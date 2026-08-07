// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "CoreMinimal.h"
#include "MinimapTypes.h"
#include "MinimapIconLayerWidget.generated.h"

class UMinimapSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapIconClicked, AActor*, Actor);

/**
 * Paints every map icon (tracked actors, objectives, the player marker and an optional unstyled
 * map background) in a single Slate paint pass. Owning map widgets push a FMinimapViewState each
 * frame; the layer projects world positions through it, applies edge clamping and records icon
 * rects for click hit-testing.
 */
UCLASS()
class MODULARMINIMAP_API UMinimapIconLayerWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/** Icon used for the local player's pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FMinimapIconStyle PlayerIconStyle;

	/** Draws the raw map background under the icons. Enabled automatically when no styling material is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bDrawMapFallback = false;

	/** Tint applied to the fallback map background. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FLinearColor FallbackMapTint = FLinearColor(1.0f, 1.0f, 1.0f, 0.9f);

	/** Enables click hit-testing on icons (used by the full-screen map). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bClickableIcons = false;

	/** Padding kept between edge-clamped icons and the view border, in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	float EdgeClampPadding = 8.0f;

	/** Broadcast when a tracked actor's icon is clicked (requires bClickableIcons). */
	UPROPERTY(BlueprintAssignable, Category = "Minimap")
	FOnMinimapIconClicked OnIconClicked;

	/** Broadcast when an objective marker is clicked (requires bClickableIcons). */
	UPROPERTY(BlueprintAssignable, Category = "Minimap")
	FOnMinimapObjectiveClicked OnObjectiveClicked;

	/** Directional arrow drawn next to edge-clamped ClampWithArrow icons. Skipped while unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FSlateBrush EdgeArrowBrush;

	/** Size of the edge arrow in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FVector2D EdgeArrowSize = FVector2D(10.0, 10.0);

	UMinimapIconLayerWidget();

	/** Pushed by the owning map widget each frame. */
	void SetViewState(const FMinimapViewState& InViewState) { ViewState = InViewState; }
	const FMinimapViewState& GetViewState() const { return ViewState; }

protected:
	virtual void NativeOnInitialized() override;

	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	/** One icon prepared during paint. */
	struct FPaintedIcon
	{
		FVector2D TopLeft = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		float AngleRadians = 0.0f;
		FLinearColor Tint = FLinearColor::White;
		const FSlateBrush* Brush = nullptr;
		int32 ZOrder = 0;
		TWeakObjectPtr<AActor> Actor;
		FMinimapObjectiveHandle Objective;
		bool bDrawEdgeArrow = false;
		float EdgeArrowAngleRadians = 0.0f;
	};

	/** One clickable icon rect recorded during paint (widget-local space). */
	struct FIconHit
	{
		FVector2D Center = FVector2D::ZeroVector;
		FVector2D HalfSize = FVector2D::ZeroVector;
		TWeakObjectPtr<AActor> Actor;
		FMinimapObjectiveHandle Objective;
	};

	/**
	 * Project a world location into widget-local space, applying the mask containment and the
	 * given edge-clamp mode. Returns false when the icon should not be drawn.
	 */
	bool ProjectAndClamp(const FMinimapProjection& Projection, const FVector2D& LocalSize, const FVector& WorldLocation,
		EMinimapEdgeClampMode ClampMode, FVector2D& OutLocalPos, bool& bOutClamped) const;

	/** Screen-space rotation for an actor-facing icon, in radians (icon art assumed to point up). */
	float ComputeIconAngle(float WorldYawDegrees) const;

	FMinimapViewState ViewState;

	mutable TArray<FIconHit> LastPaintHits;
};
