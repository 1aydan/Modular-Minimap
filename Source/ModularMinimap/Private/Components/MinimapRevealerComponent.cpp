// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MinimapRevealerComponent.h"

#include "Subsystems/MinimapSubsystem.h"

void UMinimapRevealerComponent::BeginPlay()
{
	Super::BeginPlay();

	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->RegisterRevealer(this);
	}
}

void UMinimapRevealerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->UnregisterRevealer(this);
	}

	Super::EndPlay(EndPlayReason);
}
