// Copyright Epic Games, Inc. All Rights Reserved.

#include "MinimapBakeUtility.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CanvasItem.h"
#include "Data/MinimapLevelSettings.h"
#include "Editor.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GlobalRenderResources.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "MinimapTypes.h"
#include "Misc/PackageName.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogModularMinimapEditor, Log, All);

static constexpr int32 BakeTextureSize = 2048;
static constexpr float BakeBoundsMargin = 0.05f;

static bool SaveAsset(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);

	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
}

void FMinimapBakeUtility::BakeCurrentLevel()
{
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World == nullptr)
	{
		UE_LOG(LogModularMinimapEditor, Warning, TEXT("Bake: no editor world."));
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	ARecastNavMesh* NavMesh = NavSys != nullptr ? Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance()) : nullptr;
	if (NavMesh == nullptr)
	{
		UE_LOG(LogModularMinimapEditor, Warning, TEXT("Bake: no recast navmesh in this level."));
		return;
	}

	FRecastDebugGeometry Geometry;
	Geometry.bGatherNavMeshEdges = 1;
	NavMesh->GetDebugGeometryForTile(Geometry, FNavTileRef());

	if (Geometry.MeshVerts.IsEmpty())
	{
		UE_LOG(LogModularMinimapEditor, Warning,
			TEXT("Bake: navmesh has no tiles. If this project generates navigation only around invokers, this level cannot be baked in-editor."));
		return;
	}

	// Bounds from geometry.
	FBox2D Bounds(ForceInit);
	for (const FVector& Vertex : Geometry.MeshVerts)
	{
		Bounds += FVector2D(Vertex.X, Vertex.Y);
	}
	const FVector2D Extent = Bounds.GetExtent();
	const double Margin = FMath::Max(Extent.X, Extent.Y) * BakeBoundsMargin;
	Bounds = FBox2D(Bounds.Min - FVector2D(Margin, Margin), Bounds.Max + FVector2D(Margin, Margin));

	FMinimapProjection Projection;
	Projection.SetFromBounds(Bounds);

	const UMinimapLevelSettings* StyleDefaults = GetDefault<UMinimapLevelSettings>();

	// Render fill + edges.
	UTextureRenderTarget2D* RenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
		World, BakeTextureSize, BakeTextureSize, RTF_RGBA8, FLinearColor::Transparent, false, false);
	if (RenderTarget == nullptr)
	{
		return;
	}

	const FIntPoint TextureSize(BakeTextureSize, BakeTextureSize);

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(World, RenderTarget, Canvas, CanvasSize, Context);
	if (Canvas != nullptr)
	{
		TArray<FCanvasUVTri> Triangles;
		for (int32 AreaIndex = 0; AreaIndex < RECAST_MAX_AREAS; ++AreaIndex)
		{
			const TArray<int32>& Indices = Geometry.AreaIndices[AreaIndex];
			for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
			{
				FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
				Triangle.V0_Pos = Projection.WorldToPixel(Geometry.MeshVerts[Indices[Index + 0]], TextureSize);
				Triangle.V1_Pos = Projection.WorldToPixel(Geometry.MeshVerts[Indices[Index + 1]], TextureSize);
				Triangle.V2_Pos = Projection.WorldToPixel(Geometry.MeshVerts[Indices[Index + 2]], TextureSize);
				Triangle.V0_Color = StyleDefaults->WalkableFillColor;
				Triangle.V1_Color = StyleDefaults->WalkableFillColor;
				Triangle.V2_Color = StyleDefaults->WalkableFillColor;
			}
		}

		if (Triangles.Num() > 0)
		{
			FCanvasTriangleItem TriangleItem(Triangles, GWhiteTexture);
			TriangleItem.BlendMode = SE_BLEND_Opaque;
			Canvas->DrawItem(TriangleItem);
		}

		for (int32 Index = 0; Index + 1 < Geometry.NavMeshEdges.Num(); Index += 2)
		{
			const FVector2D Start = Projection.WorldToPixel(Geometry.NavMeshEdges[Index], TextureSize);
			const FVector2D End = Projection.WorldToPixel(Geometry.NavMeshEdges[Index + 1], TextureSize);
			FCanvasLineItem Line(Start, End);
			Line.SetColor(StyleDefaults->OutlineColor);
			// A bake burns the outline into the texture, so the style's screen-pixel width lands here
			// as a texture-pixel width. Baked backgrounds bypass the material's distance shading.
			Line.LineThickness = StyleDefaults->OutlineWidthPixels;
			Canvas->DrawItem(Line);
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(World, Context);

	// Save texture asset next to the level.
	const FString LevelPackage = World->GetOutermost()->GetName();
	const FString TextureName = FPackageName::GetShortName(LevelPackage) + TEXT("_MinimapTexture");
	const FString TexturePackagePath = FPackageName::GetLongPackagePath(LevelPackage) / TextureName;

	UTexture2D* BakedTexture = UKismetRenderingLibrary::RenderTargetCreateStaticTexture2DEditorOnly(
		RenderTarget, TexturePackagePath, TC_EditorIcon, TMGS_NoMipmaps);
	if (BakedTexture == nullptr)
	{
		UE_LOG(LogModularMinimapEditor, Warning, TEXT("Bake: failed to create texture asset at %s."), *TexturePackagePath);
		return;
	}
	SaveAsset(BakedTexture);

	// Create or update the level settings asset.
	const FString SettingsName = FPackageName::GetShortName(LevelPackage) + TEXT("_MinimapSettings");
	const FString SettingsPackagePath = FPackageName::GetLongPackagePath(LevelPackage) / SettingsName;

	UMinimapLevelSettings* Settings = LoadObject<UMinimapLevelSettings>(nullptr, *(SettingsPackagePath + TEXT(".") + SettingsName));
	if (Settings == nullptr)
	{
		UPackage* SettingsPackage = CreatePackage(*SettingsPackagePath);
		Settings = NewObject<UMinimapLevelSettings>(SettingsPackage, *SettingsName, RF_Public | RF_Standalone);
	}

	Settings->BackgroundTexture = BakedTexture;
	Settings->BackgroundWorldBounds = FBox2D(
		FVector2D(Projection.SquareCenter.X - Projection.WorldSize * 0.5, Projection.SquareCenter.Y - Projection.WorldSize * 0.5),
		FVector2D(Projection.SquareCenter.X + Projection.WorldSize * 0.5, Projection.SquareCenter.Y + Projection.WorldSize * 0.5));
	SaveAsset(Settings);

	UE_LOG(LogModularMinimapEditor, Log, TEXT("Bake: wrote %s and %s (bounds min=%s max=%s)."),
		*TexturePackagePath, *SettingsPackagePath,
		*Bounds.Min.ToString(), *Bounds.Max.ToString());
}
