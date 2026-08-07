// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ModularMinimap : ModuleRules
{
	public ModularMinimap(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"UMG",
				"Slate",
				"SlateCore",
				"NavigationSystem",
				"GameplayTags",
				"DeveloperSettings",
				"CommonUI",
				"CommonInput"
			});

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"RenderCore",
				"RHI",
				// Recast headers, pulled in by NavMesh/RecastNavMeshGenerator.h: the navmesh capture
				// has to know whether the generator is restricted to its active tile set.
				"Navmesh"
			});
	}
}
