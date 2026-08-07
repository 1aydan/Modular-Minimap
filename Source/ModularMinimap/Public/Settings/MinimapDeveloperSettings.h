// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MinimapDeveloperSettings.generated.h"

/**
 * Project-wide defaults for the Modular Minimap plugin (Project Settings > Plugins > Modular Minimap).
 * Per-level values from UMinimapLevelSettings take precedence where they overlap.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Modular Minimap"))
class MODULARMINIMAP_API UMinimapDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }

	/** Resolution of the auto-generated walkable-coverage render target. */
	UPROPERTY(EditAnywhere, Config, Category = "Capture", meta = (ClampMin = 128, ClampMax = 4096))
	int32 CoverageTextureSize = 1024;

	/** Maximum navmesh tiles stamped into the coverage texture per frame. */
	UPROPERTY(EditAnywhere, Config, Category = "Capture", meta = (ClampMin = 1, ClampMax = 64))
	int32 MaxTilesStampedPerFrame = 8;

	/** Fallback poll interval in seconds for detecting new navmesh tiles. */
	UPROPERTY(EditAnywhere, Config, Category = "Capture", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float TilePollInterval = 0.5f;

	/** Fractional margin added around auto-resolved map bounds so late tiles rarely force a re-projection. */
	UPROPERTY(EditAnywhere, Config, Category = "Capture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AutoBoundsMargin = 0.1f;

	/** Master switch for fog of war. When false, no fog targets are created and everything is visible. */
	UPROPERTY(EditAnywhere, Config, Category = "Fog")
	bool bEnableFogOfWar = true;

	/** Brightness multiplier for explored-but-not-visible areas (0 = hidden, 1 = full). */
	UPROPERTY(EditAnywhere, Config, Category = "Fog", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ExploredDimFactor = 0.45f;

	/** Resolution of the fog-of-war render targets. */
	UPROPERTY(EditAnywhere, Config, Category = "Fog", meta = (ClampMin = 64, ClampMax = 2048))
	int32 FogTextureSize = 512;

	/** Seconds between fog reveal updates. */
	UPROPERTY(EditAnywhere, Config, Category = "Fog", meta = (ClampMin = "0.02", ClampMax = "2.0"))
	float FogUpdateInterval = 0.2f;

	/** Minimum world units a revealer must move before the explored mask is restamped. */
	UPROPERTY(EditAnywhere, Config, Category = "Fog", meta = (ClampMin = "0.0"))
	float FogMinRevealerMove = 100.0f;
};
