// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MinimapTypes.h"
#include "MinimapTrackerComponent.generated.h"

/**
 * Add to any actor to show it on the minimap, full-screen map and (optionally) the compass.
 * Registers with the minimap subsystem on BeginPlay and unregisters on EndPlay.
 */
UCLASS(ClassGroup = (Minimap), meta = (BlueprintSpawnableComponent))
class MODULARMINIMAP_API UMinimapTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Icon appearance and behavior for the tracked actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FMinimapIconStyle IconStyle;

	/** Hide/show this tracker without unregistering. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void SetTrackerVisible(bool bVisible) { bTrackerVisible = bVisible; }

	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsTrackerVisible() const { return bTrackerVisible; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	bool bTrackerVisible = true;
};
