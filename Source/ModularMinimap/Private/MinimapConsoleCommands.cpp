// Copyright Epic Games, Inc. All Rights Reserved.

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ModularMinimapModule.h"
#include "Subsystems/MinimapSubsystem.h"


static TAutoConsoleVariable<float> CVarAutoDumpAfterSeconds(
TEXT("ModularMinimap.AutoDumpAfterSeconds"),
0.0f,
TEXT("When > 0, the minimap subsystem dumps coverage stats and a PNG this many seconds after world begin play. For headless verification."));

static FAutoConsoleCommandWithWorldAndArgs GDumpCoverageCommand(
TEXT("ModularMinimap.DumpCoverage"),
TEXT("Logs minimap capture stats and exports the coverage render target to Saved/ModularMinimap/."),
FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(World);
	if (Subsystem)
	{
		Subsystem->DumpCoverageToSaved();
	}
	else
	{
		UE_LOG(LogModularMinimap, Warning, TEXT("DumpCoverage: no minimap subsystem in this world."));
	}
}));

static FAutoConsoleCommandWithWorldAndArgs GSetFogEnabledCommand(
TEXT("ModularMinimap.SetFogEnabled"),
TEXT("Turns fog of war on (1) or off (0) for this world, or clears the runtime override (reset)."),
FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(World);
	if (Subsystem == nullptr)
	{
		UE_LOG(LogModularMinimap, Warning, TEXT("SetFogEnabled: no minimap subsystem in this world."));
		return;
	}

	if (Args.Num() == 0)
	{
		UE_LOG(LogModularMinimap, Log, TEXT("Fog of war is currently %s."), Subsystem->IsFogOfWarEnabled() ? TEXT("enabled") : TEXT("disabled"));
		return;
	}

	if (Args[0].Equals(TEXT("reset"), ESearchCase::IgnoreCase))
	{
		Subsystem->ClearFogOfWarOverride();
	}
	else
	{
		Subsystem->SetFogOfWarEnabled(Args[0].ToBool());
	}
}));

