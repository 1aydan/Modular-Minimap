// Copyright Epic Games, Inc. All Rights Reserved.

#include "ModularMinimapEditorModule.h"

#include "HAL/IConsoleManager.h"
#include "MinimapAssetFactory.h"
#include "MinimapBakeUtility.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

static FAutoConsoleCommand GCreateDefaultAssetsCommand(
	TEXT("ModularMinimap.CreateDefaultAssets"),
	TEXT("Creates the plugin's default material and icon textures under /ModularMinimap (skips existing assets). Pass 'force' to rebuild the styling material in place."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const bool bForce = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("force"), ESearchCase::IgnoreCase); });
		FMinimapAssetFactory::CreateDefaultAssets(bForce);
	}));

static FAutoConsoleCommand GValidateDefaultMaterialCommand(
	TEXT("ModularMinimap.ValidateDefaultMaterial"),
	TEXT("Compiles the generated styling material and logs VALIDATE_RESULT: PASS or FAIL with any shader compile errors."),
	FConsoleCommandDelegate::CreateLambda([]() { FMinimapAssetFactory::ValidateDefaultMaterial(); }));

void FModularMinimapEditorModule::StartupModule()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
	{
		FToolMenuOwnerScoped OwnerScoped(TEXT("ModularMinimapEditor"));

		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
		FToolMenuSection& Section = Menu->FindOrAddSection("ModularMinimap", NSLOCTEXT("ModularMinimap", "SectionLabel", "Modular Minimap"));
		Section.AddMenuEntry(
			"BakeMinimapBackground",
			NSLOCTEXT("ModularMinimap", "BakeEntryLabel", "Bake Minimap Background"),
			NSLOCTEXT("ModularMinimap", "BakeEntryTooltip", "Render the current level's navmesh into a minimap background texture and create/update its minimap level settings asset."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&FMinimapBakeUtility::BakeCurrentLevel)));
		Section.AddMenuEntry(
			"CreateDefaultMinimapAssets",
			NSLOCTEXT("ModularMinimap", "CreateAssetsEntryLabel", "Create Default Minimap Assets"),
			NSLOCTEXT("ModularMinimap", "CreateAssetsEntryTooltip", "Create the plugin's default styling material and icon textures under /ModularMinimap (skips existing assets)."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([]() { FMinimapAssetFactory::CreateDefaultAssets(true); })));
	}));
}

void FModularMinimapEditorModule::ShutdownModule()
{
	UToolMenus* ToolMenus = UToolMenus::TryGet();
	if (ToolMenus)
	{
		ToolMenus->UnregisterOwnerByName(TEXT("ModularMinimapEditor"));
	}
}

IMPLEMENT_MODULE(FModularMinimapEditorModule, ModularMinimapEditor)
