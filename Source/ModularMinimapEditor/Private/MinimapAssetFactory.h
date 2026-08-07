// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Creates the plugin's default content assets into /ModularMinimap:
 *  - Materials/M_MinimapBase: the styling/compositing material (PoE-style fill + outline from the
 *    coverage mask, fog composite, circular/rectangular mask) driven by the widget base's
 *    documented parameters,
 *  - Textures/TX_PlayerArrow, TX_EdgeArrow, TX_ObjectiveMarker, TX_DefaultDot: generated icons.
 *
 * Runnable from Tools > Modular Minimap or via `ModularMinimap.CreateDefaultAssets [force]`.
 * Existing assets are skipped, so it is safe to re-run; pass force to rebuild the styling
 * material in place (used when its generated graph changes).
 */
class FMinimapAssetFactory
{
public:
	static void CreateDefaultAssets(bool bForceRebuildMaterial = false);

	/**
	 * Loads the generated styling material, forces its shaders to finish compiling, and logs any
	 * compile errors. Returns true when the material compiled clean. Exposed as
	 * `ModularMinimap.ValidateDefaultMaterial` so generation can be checked headlessly.
	 */
	static bool ValidateDefaultMaterial();
};
