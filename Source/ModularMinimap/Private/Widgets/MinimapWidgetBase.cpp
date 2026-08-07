// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/MinimapWidgetBase.h"

#include "Blueprint/WidgetTree.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Data/MinimapLevelSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "Settings/MinimapDeveloperSettings.h"
#include "Subsystems/MinimapSubsystem.h"
#include "Widgets/MinimapIconLayerWidget.h"

// Parameter names the styling material is expected to expose; see the class comment for the contract.
static const FName MinimapParam_MapTexture(TEXT("MapTexture"));
static const FName MinimapParam_FogExplored(TEXT("FogExplored"));
static const FName MinimapParam_FogVisible(TEXT("FogVisible"));
static const FName MinimapParam_CenterUV(TEXT("CenterUV"));
static const FName MinimapParam_AspectScale(TEXT("AspectScale"));
static const FName MinimapParam_FillColor(TEXT("FillColor"));
static const FName MinimapParam_OutlineColor(TEXT("OutlineColor"));
static const FName MinimapParam_UVSpan(TEXT("UVSpan"));
static const FName MinimapParam_RotationRad(TEXT("RotationRad"));
static const FName MinimapParam_MaskShape(TEXT("MaskShape"));
static const FName MinimapParam_MapTexelCount(TEXT("MapTexelCount"));
static const FName MinimapParam_ExploredDim(TEXT("ExploredDim"));
static const FName MinimapParam_FogEnabled(TEXT("FogEnabled"));
static const FName MinimapParam_BackgroundIsMask(TEXT("BackgroundIsMask"));
static const FName MinimapParam_GlowColor(TEXT("GlowColor"));
static const FName MinimapParam_OutlineWidthPixels(TEXT("OutlineWidthPixels"));
static const FName MinimapParam_OutlineSoftnessPixels(TEXT("OutlineSoftnessPixels"));
static const FName MinimapParam_OutlineOffsetPixels(TEXT("OutlineOffsetPixels"));
static const FName MinimapParam_FillFeatherPixels(TEXT("FillFeatherPixels"));
static const FName MinimapParam_GlowWidthPixels(TEXT("GlowWidthPixels"));

void UMinimapWidgetBase::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	EnsureWidgetTree();
}

void UMinimapWidgetBase::NativeConstruct()
{
	Super::NativeConstruct();

	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->OnMapTextureChanged.AddUniqueDynamic(this, &UMinimapWidgetBase::HandleMapTextureChanged);
	}

	RefreshMapBrush();
}

void UMinimapWidgetBase::NativeDestruct()
{
	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->OnMapTextureChanged.RemoveDynamic(this, &UMinimapWidgetBase::HandleMapTextureChanged);
	}

	Super::NativeDestruct();
}

void UMinimapWidgetBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem == nullptr)
	{
		return;
	}

	const FMinimapProjection Projection = Subsystem->GetProjection();
	if (!Projection.IsValid())
	{
		return;
	}

	// The icon layer letterboxes: it projects UVSpan across the widget's smaller dimension and lets
	// the longer axis show proportionally more world. The material samples from a plain 0..1 UV, so
	// it needs the same correction or map features drift away from their icons on a non-square view.
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	const double Side = FMath::Min(LocalSize.X, LocalSize.Y);
	const FVector2D AspectScale = Side > UE_DOUBLE_KINDA_SMALL_NUMBER
		? FVector2D(LocalSize.X / Side, LocalSize.Y / Side)
		: FVector2D(1.0, 1.0);

	ViewState.CenterUV = Projection.WorldToUV(GetViewCenterWorld());
	ViewState.UVSpan = FMath::Max(static_cast<double>(ViewWorldSpan) / Projection.WorldSize, 0.001);
	ViewState.RotationRadians = bRotateWithView ? FMath::DegreesToRadians(GetViewYawDegrees()) : 0.0f;
	ViewState.MaskShape = MaskShape;

	if (bClampViewToBounds)
	{
		ViewState.CenterUV = ClampCenterToBounds(ViewState.CenterUV, ViewState.UVSpan, ViewState.RotationRadians, AspectScale);
	}

	if (IconLayer != nullptr)
	{
		IconLayer->SetViewState(ViewState);
	}

	PushMaterialParameters(AspectScale);
}

void UMinimapWidgetBase::SetViewWorldSpan(float InSpan)
{
	ViewWorldSpan = FMath::Max(InSpan, 100.0f);
}

void UMinimapWidgetBase::ZoomBy(float Multiplier)
{
	if (Multiplier > UE_KINDA_SMALL_NUMBER)
	{
		SetViewWorldSpan(ViewWorldSpan * Multiplier);
	}
}

FVector UMinimapWidgetBase::GetViewCenterWorld() const
{
	const APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController)
	{
		const APawn* Pawn = PlayerController->GetPawn();
		if (Pawn)
		{
			return Pawn->GetActorLocation();
		}

		if (PlayerController->PlayerCameraManager != nullptr)
		{
			return PlayerController->PlayerCameraManager->GetCameraLocation();
		}
	}

	return FVector::ZeroVector;
}

float UMinimapWidgetBase::GetViewYawDegrees() const
{
	const UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		return Subsystem->GetViewYawDegrees(GetOwningPlayer());
	}

	const APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController)
	{
		if (PlayerController->PlayerCameraManager != nullptr)
		{
			return PlayerController->PlayerCameraManager->GetCameraRotation().Yaw;
		}
	}

	return 0.0f;
}

void UMinimapWidgetBase::EnsureWidgetTree()
{
	if (WidgetTree == nullptr || WidgetTree->RootWidget != nullptr)
	{
		return;
	}

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("AutoRoot"));
	WidgetTree->RootWidget = Root;

	MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("AutoMapImage"));
	UOverlaySlot* ImageSlot = Root->AddChildToOverlay(MapImage);
	if (ImageSlot)
	{
		ImageSlot->SetHorizontalAlignment(HAlign_Fill);
		ImageSlot->SetVerticalAlignment(VAlign_Fill);
	}

	IconLayer = WidgetTree->ConstructWidget<UMinimapIconLayerWidget>(UMinimapIconLayerWidget::StaticClass(), TEXT("AutoIconLayer"));
	UOverlaySlot* LayerSlot = Root->AddChildToOverlay(IconLayer);
	if (LayerSlot)
	{
		LayerSlot->SetHorizontalAlignment(HAlign_Fill);
		LayerSlot->SetVerticalAlignment(VAlign_Fill);
	}

	SetClipping(EWidgetClipping::ClipToBounds);
}

void UMinimapWidgetBase::RefreshMapBrush()
{
	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);

	// Fall back to the plugin's generated styling material when none was assigned.
	if (MapMaterial == nullptr && FPackageName::DoesPackageExist(TEXT("/ModularMinimap/Materials/M_MinimapBase")))
	{
		MapMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/ModularMinimap/Materials/M_MinimapBase.M_MinimapBase"));
	}

	if (MapMaterial != nullptr && MapImage != nullptr)
	{
		if (MapMID == nullptr || MapMID->Parent != MapMaterial)
		{
			MapMID = UMaterialInstanceDynamic::Create(MapMaterial, this);
			MapImage->SetBrushFromMaterial(MapMID);
		}

		if (Subsystem != nullptr && MapMID != nullptr)
		{
			UTexture* MapTexture = Subsystem->GetBackgroundTexture();
			if (MapTexture)
			{
				MapMID->SetTextureParameterValue(MinimapParam_MapTexture, MapTexture);

				// Coverage render targets carry a walkability mask that the material styles;
				// authored textures are full-color and display as-is.
				const bool bIsMask = MapTexture == Subsystem->GetCoverageRenderTarget();
				MapMID->SetScalarParameterValue(MinimapParam_BackgroundIsMask, bIsMask ? 1.0f : 0.0f);
			}

			float TexelCount = 1024.0f;
			const UTextureRenderTarget2D* Coverage = Subsystem->GetCoverageRenderTarget();
			if (Coverage)
			{
				TexelCount = static_cast<float>(Coverage->SizeX);
			}
			MapMID->SetScalarParameterValue(MinimapParam_MapTexelCount, TexelCount);

			const UMinimapLevelSettings* LevelSettings = Subsystem->GetLevelSettings();
			const UMinimapLevelSettings* Defaults = GetDefault<UMinimapLevelSettings>();
			const UMinimapLevelSettings* Style = LevelSettings != nullptr ? LevelSettings : Defaults;
			MapMID->SetVectorParameterValue(MinimapParam_FillColor, Style->WalkableFillColor);
			MapMID->SetVectorParameterValue(MinimapParam_OutlineColor, Style->OutlineColor);
			MapMID->SetVectorParameterValue(MinimapParam_GlowColor, Style->GlowColor);
			MapMID->SetScalarParameterValue(MinimapParam_OutlineWidthPixels, Style->OutlineWidthPixels);
			MapMID->SetScalarParameterValue(MinimapParam_OutlineSoftnessPixels, Style->OutlineSoftnessPixels);
			MapMID->SetScalarParameterValue(MinimapParam_OutlineOffsetPixels, Style->OutlineOffsetPixels);
			MapMID->SetScalarParameterValue(MinimapParam_FillFeatherPixels, Style->FillFeatherPixels);
			MapMID->SetScalarParameterValue(MinimapParam_GlowWidthPixels, Style->GlowWidthPixels);

			const UMinimapDeveloperSettings* DevSettings = GetDefault<UMinimapDeveloperSettings>();
			UTextureRenderTarget2D* FogExplored = Subsystem->GetFogExploredRenderTarget();
			UTextureRenderTarget2D* FogVisible = Subsystem->GetFogVisibleRenderTarget();
			const bool bFogActive = DevSettings->bEnableFogOfWar && FogExplored != nullptr && FogVisible != nullptr;
			if (bFogActive)
			{
				MapMID->SetTextureParameterValue(MinimapParam_FogExplored, FogExplored);
				MapMID->SetTextureParameterValue(MinimapParam_FogVisible, FogVisible);
			}
			MapMID->SetScalarParameterValue(MinimapParam_FogEnabled, bFogActive ? 1.0f : 0.0f);
			MapMID->SetScalarParameterValue(MinimapParam_ExploredDim, DevSettings->ExploredDimFactor);
		}

		if (IconLayer != nullptr)
		{
			IconLayer->bDrawMapFallback = false;
		}

		if (MapImage != nullptr)
		{
			MapImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
	else
	{
		// No styling material: hide the image and let the icon layer draw the raw map texture.
		if (MapImage != nullptr)
		{
			MapImage->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (IconLayer != nullptr)
		{
			IconLayer->bDrawMapFallback = true;
		}
	}
}

FVector2D UMinimapWidgetBase::ClampCenterToBounds(const FVector2D& CenterUV, double UVSpan, float RotationRadians, const FVector2D& AspectScale)
{
	// Half-extent of the view in map UV, per axis. A rotated view sweeps its own corners, so use the
	// half-diagonal on both axes rather than solving the rotated rectangle exactly.
	FVector2D HalfExtent = FVector2D(UVSpan * 0.5 * AspectScale.X, UVSpan * 0.5 * AspectScale.Y);
	if (!FMath::IsNearlyZero(RotationRadians))
	{
		const double HalfDiagonal = HalfExtent.Size();
		HalfExtent = FVector2D(HalfDiagonal, HalfDiagonal);
	}

	// An axis where the view is wider than the map cannot avoid void; center it so it is symmetric.
	auto ClampAxis = [](double Center, double Half) -> double
	{
		return Half * 2.0 >= 1.0 ? 0.5 : FMath::Clamp(Center, Half, 1.0 - Half);
	};

	return FVector2D(ClampAxis(CenterUV.X, HalfExtent.X), ClampAxis(CenterUV.Y, HalfExtent.Y));
}

void UMinimapWidgetBase::PushMaterialParameters(const FVector2D& AspectScale)
{
	if (MapMID == nullptr)
	{
		return;
	}

	MapMID->SetVectorParameterValue(MinimapParam_CenterUV,
		FLinearColor(static_cast<float>(ViewState.CenterUV.X), static_cast<float>(ViewState.CenterUV.Y), 0.0f, 0.0f));
	MapMID->SetVectorParameterValue(MinimapParam_AspectScale,
		FLinearColor(static_cast<float>(AspectScale.X), static_cast<float>(AspectScale.Y), 0.0f, 0.0f));
	MapMID->SetScalarParameterValue(MinimapParam_UVSpan, static_cast<float>(ViewState.UVSpan));
	MapMID->SetScalarParameterValue(MinimapParam_RotationRad, ViewState.RotationRadians);
	MapMID->SetScalarParameterValue(MinimapParam_MaskShape, ViewState.MaskShape == EMinimapMaskShape::Circle ? 1.0f : 0.0f);
}

void UMinimapWidgetBase::HandleMapTextureChanged()
{
	RefreshMapBrush();
}
