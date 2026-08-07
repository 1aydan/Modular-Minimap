// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

MODULARMINIMAP_API DECLARE_LOG_CATEGORY_EXTERN(LogModularMinimap, Log, All);

class FModularMinimapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
