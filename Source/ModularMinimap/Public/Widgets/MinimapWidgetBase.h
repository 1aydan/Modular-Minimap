// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "CoreMinimal.h"
#include "MinimapTypes.h"
#include "MinimapWidgetBase.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMinimapIconLayerWidget;
class UMinimapSubsystem;

/**
 * Shared base for map view widgets (corner minimap and full-screen map). Owns the view state
 * (center, zoom, rotation, mask), drives the optional styling material, and feeds the icon layer.
 *
 * Works with zero assets: when neither a widget-blueprint tree nor a styling material is provided,
 * the widget auto-builds an Image + icon layer tree and draws the raw map texture directly.
 *
 * When a styling material is set it must expose these parameters:
 *   Textures: MapTexture, FogExplored, FogVisible
 *   Vectors:  CenterUV (UV in RG), FillColor, OutlineColor
 *   Scalars:  UVSpan, RotationRad, MaskShape (0 = rectangle, 1 = circle), MapTexelCount,
 *             OutlineTexels, ExploredDim, FogEnabled
 */
UCLASS(Abstract)
class MODULARMINIMAP_API UMinimapWidgetBase : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Optional styling/compositing material; when unset the raw map texture is drawn unstyled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	TObjectPtr<UMaterialInterface> MapMaterial;

	/** Mask shape for icon containment (and the material mask when styled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	EMinimapMaskShape MaskShape = EMinimapMaskShape::Circle;

	/** Rotate the map so the current view yaw points up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bRotateWithView = true;

	/** World units visible across the widget's smaller dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = "100.0"))
	float ViewWorldSpan = 6000.0f;

	/**
	 * Keep the view inside the map bounds so off-map void never enters the frame. The view center
	 * stops at the boundary, so the tracked actor drifts off-center near level edges — right for a
	 * pannable full map, usually wrong for a corner minimap where the player expects to stay centered.
	 * Ignored on any axis where the view is larger than the map; that axis centers instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bClampViewToBounds = false;

	/** Map background image. Optional in widget blueprints; auto-created when the tree is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap", meta = (BindWidgetOptional))
	TObjectPtr<UImage> MapImage;

	/** Icon overlay. Optional in widget blueprints; auto-created when the tree is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap", meta = (BindWidgetOptional))
	TObjectPtr<UMinimapIconLayerWidget> IconLayer;

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void SetViewWorldSpan(float InSpan);

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void ZoomBy(float Multiplier);

	UFUNCTION(BlueprintPure, Category = "Minimap")
	FMinimapViewState GetViewState() const { return ViewState; }

	UFUNCTION(BlueprintPure, Category = "Minimap")
	UMinimapIconLayerWidget* GetIconLayer() const { return IconLayer; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** World location the view centers on. Default: the owning player's pawn (or camera) location. */
	virtual FVector GetViewCenterWorld() const;

	/** View yaw in degrees used when bRotateWithView is set. Default: the owning player's camera yaw. */
	virtual float GetViewYawDegrees() const;

	/** Builds Image + icon layer programmatically when no widget-blueprint tree exists. */
	void EnsureWidgetTree();

	/** (Re)creates the material instance or enables the unstyled fallback. */
	void RefreshMapBrush();

	/** Pulls a view center back inside the map so the view rectangle never leaves the bounds. */
	static FVector2D ClampCenterToBounds(const FVector2D& CenterUV, double UVSpan, float RotationRadians, const FVector2D& AspectScale);

	/**
	 * Pushes per-frame view parameters into the material instance.
	 *
	 * AspectScale is the widget's local size divided by its smaller dimension, matching the
	 * letterboxing the icon layer applies. It is (1,1) on a square view.
	 */
	void PushMaterialParameters(const FVector2D& AspectScale);

	UFUNCTION()
	void HandleMapTextureChanged();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MapMID;

	FMinimapViewState ViewState;
};
