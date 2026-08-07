// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
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

	/** Fill color for walkable area on the generated map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor WalkableFillColor = FLinearColor(0.035f, 0.04f, 0.055f, 0.85f);

	/** Outline color for walkable-area borders on the generated map. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	FLinearColor OutlineColor = FLinearColor(0.55f, 0.55f, 0.65f, 1.0f);

	/** Outline thickness in map texels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float OutlineThickness = 1.5f;
};
