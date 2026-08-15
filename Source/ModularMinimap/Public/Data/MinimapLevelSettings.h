// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MinimapTypes.h"
#include "MinimapLevelSettings.generated.h"

class UTexture2D;

/**
 * Optional per-level minimap configuration. Reference it from an AMinimapBoundsVolume placed in
 * the level, or assign it at runtime via UMinimapSubsystem::SetLevelSettings.
 */
UCLASS(BlueprintType)
class MODULARMINIMAP_API UMinimapLevelSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Hand-authored background texture. When set, navmesh capture is disabled for the level and
	 * this texture is used as the map background; fog of war and icons still run.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Background")
	TObjectPtr<UTexture2D> BackgroundTexture;

	/** World-space XY rect the authored background spans. Required when BackgroundTexture is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Background")
	FBox2D BackgroundWorldBounds = FBox2D(ForceInit);

	/** When true, BoundsOverride fixes the map bounds instead of auto-resolving from navigation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds")
	bool bOverrideBounds = false;

	/** Explicit world-space XY map bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Bounds", meta = (EditCondition = "bOverrideBounds"))
	FBox2D BoundsOverride = FBox2D(ForceInit);

	/**
	 * Per-level fog-of-war switch. Leave on Inherit to use the project-wide setting; set Disabled for
	 * levels that should show the whole map immediately (towns, hubs), Enabled for levels that need
	 * fog even when the project default is off.
	 *
	 * A runtime call to UMinimapSubsystem::SetFogOfWarEnabled takes precedence over this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fog")
	EMinimapFogMode FogMode = EMinimapFogMode::Inherit;

	/** Fill color for walkable area on the generated map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor WalkableFillColor = FLinearColor(0.035f, 0.04f, 0.055f, 0.85f);

	/** Outline color for walkable-area borders on the generated map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor OutlineColor = FLinearColor(0.55f, 0.55f, 0.65f, 1.0f);

	/**
	 * Outline width in screen pixels, held constant across zoom levels.
	 *
	 * The styling material recovers a distance to the walkable boundary rather than dilating the
	 * coverage mask, so this and the widths below are real screen-space measurements: they do not
	 * drift with zoom, map resolution or widget size.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style|Outline", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float OutlineWidthPixels = 2.0f;

	/** Width of the outline's anti-aliased falloff in screen pixels. 0 is as crisp as the mask allows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style|Outline", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float OutlineSoftnessPixels = 1.0f;

	/** Shifts the outline band inside (+) or outside (-) the walkable boundary, in screen pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style|Outline", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float OutlineOffsetPixels = 0.0f;

	/** Width of the walkable fill's fade at its boundary, in screen pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style|Outline", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float FillFeatherPixels = 1.5f;

	/** Color of the glow bleeding outward from the outline. Its alpha scales the effect. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style|Outline")
	FLinearColor GlowColor = FLinearColor(0.35f, 0.38f, 0.55f, 0.5f);

	/** How far the outer glow reaches past the outline, in screen pixels. 0 disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style|Outline", meta = (ClampMin = "0.0", ClampMax = "24.0"))
	float GlowWidthPixels = 0.0f;
};
