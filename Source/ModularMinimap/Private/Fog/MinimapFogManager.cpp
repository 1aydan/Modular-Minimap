// Copyright Epic Games, Inc. All Rights Reserved.

#include "Fog/MinimapFogManager.h"

#include "CanvasItem.h"
#include "Components/MinimapRevealerComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Compression.h"
#include "ModularMinimapModule.h"
#include "RenderingThread.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Settings/MinimapDeveloperSettings.h"
#include "Subsystems/MinimapSubsystem.h"
#include "TextureResource.h"
#include "UnrealClient.h"

static constexpr uint32 FogBlobMagic = 0x4D4D4647; // 'MMFG'
static constexpr uint16 FogBlobVersion = 1;
static constexpr int32 RevealSpriteSize = 128;
static constexpr float InnerFullRevealFraction = 0.65f;

void UMinimapFogManager::Initialize(UMinimapSubsystem* InOwner)
{
	Owner = InOwner;
	ExploredGrid.SetNumZeroed(GridSize * GridSize);
}

void UMinimapFogManager::Deinitialize()
{
	LastStampPositions.Reset();
	StampHistory.Reset();
	ExploredGrid.Reset();
	Owner.Reset();
}

void UMinimapFogManager::Update(float DeltaTime)
{
	const UMinimapDeveloperSettings* Settings = GetDefault<UMinimapDeveloperSettings>();

	UpdateAccumulator += DeltaTime;
	if (UpdateAccumulator < Settings->FogUpdateInterval && !bForceNextUpdate)
	{
		return;
	}
	UpdateAccumulator = 0.0f;
	bForceNextUpdate = false;

	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || !OwnerSubsystem->GetProjection().IsValid())
	{
		return;
	}

	EnsureRenderTargets();
	EnsureRevealSprite();
	if (ExploredRT == nullptr || VisibleRT == nullptr)
	{
		return;
	}

	// Collect explored stamps from revealers that moved far enough.
	TArray<FRevealStamp> NewStamps;
	for (const TWeakObjectPtr<UMinimapRevealerComponent>& WeakRevealer : OwnerSubsystem->GetRevealers())
	{
		const UMinimapRevealerComponent* Revealer = WeakRevealer.Get();
		if (Revealer == nullptr || !Revealer->bRevealerEnabled)
		{
			continue;
		}

		const FVector Location = Revealer->GetComponentLocation();
		const FVector2D WorldPos(Location.X, Location.Y);
		const FObjectKey Key(Revealer);

		const FVector2D* LastPos = LastStampPositions.Find(Key);
		if (LastPos != nullptr && FVector2D::Distance(*LastPos, WorldPos) < Settings->FogMinRevealerMove)
		{
			continue;
		}

		LastStampPositions.Add(Key, WorldPos);

		FRevealStamp& Stamp = NewStamps.AddDefaulted_GetRef();
		Stamp.WorldPos = WorldPos;
		Stamp.Radius = Revealer->RevealRadius;
	}

	if (NewStamps.Num() > 0)
	{
		StampExplored(NewStamps);
		StampHistory.Append(NewStamps);
		for (const FRevealStamp& Stamp : NewStamps)
		{
			MarkGridExplored(Stamp.WorldPos, Stamp.Radius);
		}
	}

	StampVisible();
}

void UMinimapFogManager::HandleProjectionChanged()
{
	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || ExploredRT == nullptr)
	{
		return;
	}

	UWorld* World = OwnerSubsystem->GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UKismetRenderingLibrary::ClearRenderTarget2D(World, ExploredRT, FLinearColor::Black);
	UKismetRenderingLibrary::ClearRenderTarget2D(World, VisibleRT, FLinearColor::Black);

	if (StampHistory.Num() > 0)
	{
		StampExplored(StampHistory);
	}

	RebuildGridFromHistory();
}

void UMinimapFogManager::ForgetRevealer(const UMinimapRevealerComponent* Revealer)
{
	if (Revealer != nullptr)
	{
		LastStampPositions.Remove(FObjectKey(Revealer));
	}
}

bool UMinimapFogManager::IsWorldExplored(const FVector& WorldLocation) const
{
	const UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || ExploredGrid.IsEmpty())
	{
		return true;
	}

	const FMinimapProjection Projection = OwnerSubsystem->GetProjection();
	if (!Projection.IsValid())
	{
		return true;
	}

	const FVector2D UV = Projection.WorldToUV(WorldLocation);
	const int32 CellX = FMath::Clamp(static_cast<int32>(UV.X * GridSize), 0, GridSize - 1);
	const int32 CellY = FMath::Clamp(static_cast<int32>(UV.Y * GridSize), 0, GridSize - 1);
	return ExploredGrid[CellY * GridSize + CellX] != 0;
}

bool UMinimapFogManager::IsWorldVisible(const FVector& WorldLocation) const
{
	const UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr)
	{
		return false;
	}

	const FVector2D WorldPos(WorldLocation.X, WorldLocation.Y);

	for (const TWeakObjectPtr<UMinimapRevealerComponent>& WeakRevealer : OwnerSubsystem->GetRevealers())
	{
		const UMinimapRevealerComponent* Revealer = WeakRevealer.Get();
		if (Revealer == nullptr || !Revealer->bRevealerEnabled)
		{
			continue;
		}

		const FVector Location = Revealer->GetComponentLocation();
		if (FVector2D::DistSquared(FVector2D(Location.X, Location.Y), WorldPos) <= FMath::Square(Revealer->RevealRadius))
		{
			return true;
		}
	}

	return false;
}

bool UMinimapFogManager::ExportState(TArray<uint8>& OutData)
{
	OutData.Reset();

	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || ExploredRT == nullptr)
	{
		return false;
	}

	const FMinimapProjection Projection = OwnerSubsystem->GetProjection();
	if (!Projection.IsValid())
	{
		return false;
	}

	FTextureRenderTargetResource* Resource = ExploredRT->GameThread_GetRenderTargetResource();
	if (Resource == nullptr)
	{
		return false;
	}

	FlushRenderingCommands();

	TArray<FColor> Pixels;
	if (!Resource->ReadPixels(Pixels))
	{
		return false;
	}

	const uint16 Width = static_cast<uint16>(ExploredRT->SizeX);
	const uint16 Height = static_cast<uint16>(ExploredRT->SizeY);

	// Payload: mapping header + one byte per pixel (explored intensity).
	TArray<uint8> Payload;
	{
		FMemoryWriter Writer(Payload);
		uint16 PayloadWidth = Width;
		uint16 PayloadHeight = Height;
		double CenterX = Projection.SquareCenter.X;
		double CenterY = Projection.SquareCenter.Y;
		double WorldSize = Projection.WorldSize;
		Writer << PayloadWidth;
		Writer << PayloadHeight;
		Writer << CenterX;
		Writer << CenterY;
		Writer << WorldSize;

		const int64 PixelOffset = Payload.Num();
		Payload.SetNumUninitialized(PixelOffset + Width * Height);
		for (int32 Index = 0; Index < Pixels.Num() && Index < Width * Height; ++Index)
		{
			Payload[PixelOffset + Index] = Pixels[Index].R;
		}
	}

	// Blob: magic + version + uncompressed size + zlib bytes.
	int32 CompressedBound = FCompression::CompressMemoryBound(NAME_Zlib, Payload.Num());
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(CompressedBound);
	int32 CompressedSize = CompressedBound;
	if (!FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Payload.GetData(), Payload.Num()))
	{
		return false;
	}
	Compressed.SetNum(CompressedSize);

	FMemoryWriter Writer(OutData);
	uint32 Magic = FogBlobMagic;
	uint16 Version = FogBlobVersion;
	int32 UncompressedSize = Payload.Num();
	Writer << Magic;
	Writer << Version;
	Writer << UncompressedSize;
	OutData.Append(Compressed);

	return true;
}

bool UMinimapFogManager::ImportState(const TArray<uint8>& Data)
{
	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || Data.Num() <= 10)
	{
		return false;
	}

	UWorld* World = OwnerSubsystem->GetWorld();
	const FMinimapProjection Projection = OwnerSubsystem->GetProjection();
	if (World == nullptr || !Projection.IsValid())
	{
		return false;
	}

	uint32 Magic = 0;
	uint16 Version = 0;
	int32 UncompressedSize = 0;
	FMemoryReader Reader(Data);
	Reader << Magic;
	Reader << Version;
	Reader << UncompressedSize;

	if (Magic != FogBlobMagic || Version != FogBlobVersion || UncompressedSize <= 0)
	{
		UE_LOG(LogModularMinimap, Warning, TEXT("ImportState: unrecognized fog blob (magic=0x%08X version=%d)."), Magic, Version);
		return false;
	}

	const int64 CompressedOffset = Reader.Tell();
	TArray<uint8> Payload;
	Payload.SetNumUninitialized(UncompressedSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Payload.GetData(), UncompressedSize, Data.GetData() + CompressedOffset, Data.Num() - CompressedOffset))
	{
		UE_LOG(LogModularMinimap, Warning, TEXT("ImportState: zlib decompression failed."));
		return false;
	}

	uint16 Width = 0;
	uint16 Height = 0;
	double CenterX = 0.0;
	double CenterY = 0.0;
	double WorldSize = 0.0;
	FMemoryReader PayloadReader(Payload);
	PayloadReader << Width;
	PayloadReader << Height;
	PayloadReader << CenterX;
	PayloadReader << CenterY;
	PayloadReader << WorldSize;

	const int64 PixelOffset = PayloadReader.Tell();
	if (Width == 0 || Height == 0 || WorldSize <= 0.0 || Payload.Num() - PixelOffset < static_cast<int64>(Width) * Height)
	{
		UE_LOG(LogModularMinimap, Warning, TEXT("ImportState: malformed fog payload."));
		return false;
	}

	// Build a transient G8 texture from the imported mask.
	UTexture2D* ImportTexture = UTexture2D::CreateTransient(Width, Height, PF_G8);
	if (ImportTexture == nullptr)
	{
		return false;
	}
	ImportTexture->SRGB = false;
	ImportTexture->Filter = TF_Bilinear;

	void* MipData = ImportTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Payload.GetData() + PixelOffset, static_cast<SIZE_T>(Width) * Height);
	ImportTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
	ImportTexture->UpdateResource();

	EnsureRenderTargets();
	if (ExploredRT == nullptr)
	{
		return false;
	}

	// Destination rect: the imported square remapped through the current projection.
	const double HalfSize = WorldSize * 0.5;
	const FVector2D CornerA(CenterX + HalfSize, CenterY - HalfSize); // maps to imported UV (0,0)
	const FVector2D CornerB(CenterX - HalfSize, CenterY + HalfSize); // maps to imported UV (1,1)
	const FIntPoint TargetSize(ExploredRT->SizeX, ExploredRT->SizeY);
	const FVector2D PixelA = Projection.World2DToPixel(CornerA, TargetSize);
	const FVector2D PixelB = Projection.World2DToPixel(CornerB, TargetSize);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, ExploredRT, Canvas, CanvasSize, Context);
	if (Canvas != nullptr)
	{
		FCanvasTileItem Tile(PixelA, ImportTexture->GetResource(), PixelB - PixelA, FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Additive;
		Canvas->DrawItem(Tile);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);

	// Fold the imported mask into the CPU-side explored grid.
	const uint8* PixelBytes = Payload.GetData() + PixelOffset;
	for (int32 GridY = 0; GridY < GridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < GridSize; ++GridX)
		{
			// Imported-square UV of this grid cell center, via world space.
			const FVector2D CellUV((GridX + 0.5) / GridSize, (GridY + 0.5) / GridSize);
			const FVector2D CellWorld = Projection.UVToWorld2D(CellUV);
			const double ImportU = (CellWorld.Y - (CenterY - HalfSize)) / WorldSize;
			const double ImportV = 1.0 - (CellWorld.X - (CenterX - HalfSize)) / WorldSize;
			if (ImportU < 0.0 || ImportU > 1.0 || ImportV < 0.0 || ImportV > 1.0)
			{
				continue;
			}

			const int32 PixelX = FMath::Clamp(static_cast<int32>(ImportU * Width), 0, Width - 1);
			const int32 PixelY = FMath::Clamp(static_cast<int32>(ImportV * Height), 0, Height - 1);
			if (PixelBytes[PixelY * Width + PixelX] > 32)
			{
				ExploredGrid[GridY * GridSize + GridX] = 1;
			}
		}
	}

	return true;
}

void UMinimapFogManager::EnsureRenderTargets()
{
	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || ExploredRT != nullptr)
	{
		return;
	}

	UWorld* World = OwnerSubsystem->GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const UMinimapDeveloperSettings* Settings = GetDefault<UMinimapDeveloperSettings>();
	ExploredRT = UKismetRenderingLibrary::CreateRenderTarget2D(
		World, Settings->FogTextureSize, Settings->FogTextureSize, RTF_R8, FLinearColor::Black, false, false);
	VisibleRT = UKismetRenderingLibrary::CreateRenderTarget2D(
		World, Settings->FogTextureSize, Settings->FogTextureSize, RTF_R8, FLinearColor::Black, false, false);

	OwnerSubsystem->OnMapTextureChanged.Broadcast();
}

void UMinimapFogManager::EnsureRevealSprite()
{
	if (RevealSprite != nullptr)
	{
		return;
	}

	RevealSprite = UTexture2D::CreateTransient(RevealSpriteSize, RevealSpriteSize, PF_B8G8R8A8);
	if (RevealSprite == nullptr)
	{
		return;
	}

	RevealSprite->SRGB = false;
	RevealSprite->Filter = TF_Bilinear;

	FColor* MipData = static_cast<FColor*>(RevealSprite->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	const float HalfSize = RevealSpriteSize * 0.5f;
	for (int32 Y = 0; Y < RevealSpriteSize; ++Y)
	{
		for (int32 X = 0; X < RevealSpriteSize; ++X)
		{
			const float Distance = FVector2D::Distance(FVector2D(X + 0.5f, Y + 0.5f), FVector2D(HalfSize, HalfSize)) / HalfSize;
			const float Falloff = 1.0f - FMath::SmoothStep(InnerFullRevealFraction, 1.0f, Distance);
			const uint8 Value = static_cast<uint8>(FMath::Clamp(Falloff, 0.0f, 1.0f) * 255.0f);
			MipData[Y * RevealSpriteSize + X] = FColor(Value, Value, Value, Value);
		}
	}
	RevealSprite->GetPlatformData()->Mips[0].BulkData.Unlock();
	RevealSprite->UpdateResource();
}

void UMinimapFogManager::StampExplored(const TArray<FRevealStamp>& Stamps)
{
	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || ExploredRT == nullptr || RevealSprite == nullptr || Stamps.IsEmpty())
	{
		return;
	}

	UWorld* World = OwnerSubsystem->GetWorld();
	const FMinimapProjection Projection = OwnerSubsystem->GetProjection();
	if (World == nullptr || !Projection.IsValid())
	{
		return;
	}

	const FIntPoint TargetSize(ExploredRT->SizeX, ExploredRT->SizeY);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, ExploredRT, Canvas, CanvasSize, Context);
	if (Canvas != nullptr)
	{
		for (const FRevealStamp& Stamp : Stamps)
		{
			const double RadiusPixels = (Stamp.Radius / Projection.WorldSize) * TargetSize.X;
			const FVector2D Center = Projection.World2DToPixel(Stamp.WorldPos, TargetSize);
			FCanvasTileItem Tile(Center - FVector2D(RadiusPixels, RadiusPixels), RevealSprite->GetResource(), FVector2D(RadiusPixels * 2.0, RadiusPixels * 2.0), FLinearColor::White);
			Tile.BlendMode = SE_BLEND_Additive;
			Canvas->DrawItem(Tile);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
}

void UMinimapFogManager::StampVisible()
{
	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr || VisibleRT == nullptr || RevealSprite == nullptr)
	{
		return;
	}

	UWorld* World = OwnerSubsystem->GetWorld();
	const FMinimapProjection Projection = OwnerSubsystem->GetProjection();
	if (World == nullptr || !Projection.IsValid())
	{
		return;
	}

	UKismetRenderingLibrary::ClearRenderTarget2D(World, VisibleRT, FLinearColor::Black);

	const FIntPoint TargetSize(VisibleRT->SizeX, VisibleRT->SizeY);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, VisibleRT, Canvas, CanvasSize, Context);
	if (Canvas != nullptr)
	{
		for (const TWeakObjectPtr<UMinimapRevealerComponent>& WeakRevealer : OwnerSubsystem->GetRevealers())
		{
			const UMinimapRevealerComponent* Revealer = WeakRevealer.Get();
			if (Revealer == nullptr || !Revealer->bRevealerEnabled)
			{
				continue;
			}

			const FVector Location = Revealer->GetComponentLocation();
			const double RadiusPixels = (Revealer->RevealRadius / Projection.WorldSize) * TargetSize.X;
			const FVector2D Center = Projection.World2DToPixel(FVector2D(Location.X, Location.Y), TargetSize);
			FCanvasTileItem Tile(Center - FVector2D(RadiusPixels, RadiusPixels), RevealSprite->GetResource(), FVector2D(RadiusPixels * 2.0, RadiusPixels * 2.0), FLinearColor::White);
			Tile.BlendMode = SE_BLEND_Additive;
			Canvas->DrawItem(Tile);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);
}

void UMinimapFogManager::MarkGridExplored(const FVector2D& WorldPos, float Radius)
{
	UMinimapSubsystem* OwnerSubsystem = Owner.Get();
	if (OwnerSubsystem == nullptr)
	{
		return;
	}

	const FMinimapProjection Projection = OwnerSubsystem->GetProjection();
	if (!Projection.IsValid() || ExploredGrid.IsEmpty())
	{
		return;
	}

	const FVector2D CenterUV = Projection.World2DToUV(WorldPos);
	const double RadiusUV = Radius / Projection.WorldSize;
	const int32 MinX = FMath::Clamp(static_cast<int32>((CenterUV.X - RadiusUV) * GridSize), 0, GridSize - 1);
	const int32 MaxX = FMath::Clamp(static_cast<int32>((CenterUV.X + RadiusUV) * GridSize), 0, GridSize - 1);
	const int32 MinY = FMath::Clamp(static_cast<int32>((CenterUV.Y - RadiusUV) * GridSize), 0, GridSize - 1);
	const int32 MaxY = FMath::Clamp(static_cast<int32>((CenterUV.Y + RadiusUV) * GridSize), 0, GridSize - 1);
	const double RadiusUVSquared = RadiusUV * RadiusUV;

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const FVector2D CellUV((X + 0.5) / GridSize, (Y + 0.5) / GridSize);
			if (FVector2D::DistSquared(CellUV, CenterUV) <= RadiusUVSquared)
			{
				ExploredGrid[Y * GridSize + X] = 1;
			}
		}
	}
}

void UMinimapFogManager::RebuildGridFromHistory()
{
	ExploredGrid.Reset();
	ExploredGrid.SetNumZeroed(GridSize * GridSize);

	for (const FRevealStamp& Stamp : StampHistory)
	{
		MarkGridExplored(Stamp.WorldPos, Stamp.Radius);
	}
}
