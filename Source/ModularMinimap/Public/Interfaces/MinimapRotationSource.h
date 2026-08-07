// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MinimapRotationSource.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UMinimapRotationSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Supplies the yaw (degrees) the minimap and compass treat as "forward". Register an implementer
 * on the minimap subsystem when the default (player camera yaw) is wrong for your game — e.g.
 * a spring-arm-driven top-down camera.
 */
class MODULARMINIMAP_API IMinimapRotationSource
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Minimap")
	float GetMinimapYaw() const;
};
