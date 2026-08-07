// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MinimapRevealerComponent.h"

#include "Fog/MinimapFogManager.h"
#include "Subsystems/MinimapSubsystem.h"

void UMinimapRevealerComponent::BeginPlay()
{
	Super::BeginPlay();

	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		UMinimapFogManager* FogManager = Subsystem->GetFogManager();
		if (FogManager)
		{
			FogManager->RegisterRevealer(this);
		}
	}
}

void UMinimapRevealerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		UMinimapFogManager* FogManager = Subsystem->GetFogManager();
		if (FogManager)
		{
			FogManager->UnregisterRevealer(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}
