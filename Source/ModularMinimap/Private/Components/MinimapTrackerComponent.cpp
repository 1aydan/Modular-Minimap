// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/MinimapTrackerComponent.h"

#include "Subsystems/MinimapSubsystem.h"

void UMinimapTrackerComponent::BeginPlay()
{
	Super::BeginPlay();

	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->RegisterTracker(this);
	}
}

void UMinimapTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem)
	{
		Subsystem->UnregisterTracker(this);
	}

	Super::EndPlay(EndPlayReason);
}
