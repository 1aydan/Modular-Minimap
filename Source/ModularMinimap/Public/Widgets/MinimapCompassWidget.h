// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonUserWidget.h"
#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "MinimapTypes.h"
#include "MinimapCompassWidget.generated.h"

/**
 * Horizontal compass strip: cardinal letters plus any tracked actors/objectives flagged
 * bShowOnCompass, projected by bearing relative to the view yaw (rotation source aware).
 * Anchor it top-center; markers within +/- HalfFOVDegrees of forward are shown.
 */
UCLASS()
class MODULARMINIMAP_API UMinimapCompassWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	UMinimapCompassWidget();

	/** Degrees of bearing shown on each side of forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = "30.0", ClampMax = "180.0"))
	float HalfFOVDegrees = 90.0f;

	/** Font for the cardinal letters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FSlateFontInfo CardinalFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FLinearColor CardinalColor = FLinearColor(0.9f, 0.9f, 0.9f, 0.9f);

	/** Optional marker drawn at the strip center indicating forward. Skipped while unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FSlateBrush CenterMarkerBrush;

	/** Size of compass icons in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FVector2D CompassIconSize = FVector2D(14.0, 14.0);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
};
