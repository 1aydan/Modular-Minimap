// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Editor-only "Bake Minimap Background" utility. Gathers the full navmesh of the current editor
 * level, renders it PoE-style (fill + border edges) into a render target, saves it as a texture
 * asset next to the level, and creates/updates a UMinimapLevelSettings asset referencing it.
 *
 * Note: levels that only generate navmesh at runtime around navigation invokers cannot be baked;
 * the utility logs a warning when no tiles exist.
 */
class FMinimapBakeUtility
{
public:
	static void BakeCurrentLevel();
};
