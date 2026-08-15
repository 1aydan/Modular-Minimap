// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "MinimapRevealerComponent.generated.h"

/**
 * Reveals fog of war around its owner. Add to the player pawn (or anything that should grant
 * map vision). Registers with the minimap subsystem on BeginPlay; registration is independent of
 * whether fog is currently enabled, so a revealer added in a fog-free level still works if fog is
 * switched on later.
 */
UCLASS(ClassGroup = (Minimap), meta = (BlueprintSpawnableComponent))
class MODULARMINIMAP_API UMinimapRevealerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	/** Vision radius in world units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap", meta = (ClampMin = "100.0"))
	float RevealRadius = 2000.0f;

	/** When false the revealer neither explores nor grants current vision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bRevealerEnabled = true;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
