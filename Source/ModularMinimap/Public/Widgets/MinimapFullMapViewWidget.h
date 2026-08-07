// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/MinimapWidgetBase.h"
#include "MinimapFullMapViewWidget.generated.h"

/**
 * Pannable/zoomable map view used inside the full-screen map screen. North-up rectangular view by
 * default. Left-drag pans (icon clicks still work — the icon layer consumes clicks on icons),
 * mouse wheel zooms, and Pan/Recenter are BlueprintCallable so games can drive them from their
 * own (gamepad) input actions.
 */
UCLASS(Blueprintable)
class MODULARMINIMAP_API UMinimapFullMapViewWidget : public UMinimapWidgetBase
{
	GENERATED_BODY()

public:
	UMinimapFullMapViewWidget();

	/** Smallest allowed world span (max zoom-in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = "100.0"))
	float MinViewWorldSpan = 2000.0f;

	/** Largest allowed world span (max zoom-out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = "100.0"))
	float MaxViewWorldSpan = 200000.0f;

	/** Zoom multiplier applied per mouse-wheel notch (inverted for the other direction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = "1.01", ClampMax = "3.0"))
	float WheelZoomFactor = 1.2f;

	/** Pan the view by a screen-space delta in slate units (e.g. from a gamepad stick each frame). */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void PanByScreenDelta(FVector2D ScreenDelta);

	/** Center the view back on the local player and clamp zoom into range. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void Recenter();

	/** Multiply the current world span, clamped into [MinViewWorldSpan, MaxViewWorldSpan]. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void ZoomByClamped(float Multiplier);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FVector GetViewCenterWorld() const override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

	/** Current pan center in world space (XY). */
	FVector2D PanCenterWorld = FVector2D::ZeroVector;

	bool bDragging = false;
	FVector2D LastDragPosition = FVector2D::ZeroVector;
	double CachedSideLength = 1.0;
};
