// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Creates the plugin's default content assets into /ModularMinimap:
 *  - Materials/M_MinimapBase: the styling/compositing material (fill + outline from the coverage
 *    mask, fog composite, circular/rectangular mask) driven by the widget base's documented
 *    parameters. Its outline comes from a distance to the walkable boundary recovered from a
 *    blurred mask, so it stays smooth and keeps a constant screen width at any zoom; widths,
 *    softness and glow are all configurable in screen pixels,
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
