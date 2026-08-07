// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "MinimapBoundsVolume.generated.h"

class UMinimapLevelSettings;

/**
 * Optional authored map bounds. When placed in a level the minimap covers exactly this volume's
 * XY footprint and never auto-grows. May also reference the level's minimap settings asset.
 */
UCLASS(Blueprintable)
class MODULARMINIMAP_API AMinimapBoundsVolume : public AVolume
{
	GENERATED_BODY()

public:
	AMinimapBoundsVolume();

	/** Optional per-level minimap configuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	TObjectPtr<UMinimapLevelSettings> LevelSettings;

	/** XY footprint of this volume in world space. */
	FBox2D GetWorldBounds2D() const;
};
