# Modular Minimap — Tutorial

A step-by-step guide to getting a working minimap in your own project. Each step builds on the
previous one and is independently testable, so you can stop whenever you have what you need.

Nothing here is engine-fork or project specific. The only hard requirements are **Unreal Engine
5.8** and the **CommonUI** plugin (an engine plugin the minimap declares as a dependency).

---

## Step 1 — Install the plugin

Copy or symlink the plugin into your project's `Plugins/` folder. **The folder must be named
`ModularMinimap`** so it matches the `.uplugin` file:

```
YourProject/
  Plugins/
    ModularMinimap/
      ModularMinimap.uplugin
      Source/
```

If you develop the plugin from a separate repository, a directory junction keeps one copy of the
source:

```bat
mklink /J "C:\YourProject\Plugins\ModularMinimap" "C:\Path\To\Modular Minimap"
```

> If that path ever becomes a real directory instead of a link, your builds will silently compile a
> stale copy. On Windows, `Get-Item <path> | Select LinkType` should report `Junction`.

Build your editor target, then confirm the plugin is listed under Edit → Plugins. It is
`EnabledByDefault`, so there is no `.uproject` edit to make.

## Step 2 — Generate the default content

The plugin ships its look as *generated* assets rather than binary files. Run this once and commit
the result:

**Tools → Modular Minimap → Create Default Minimap Assets**, or from the console:

```
ModularMinimap.CreateDefaultAssets force
```

This creates `/ModularMinimap/Materials/M_MinimapBase` plus the icon textures. Check it worked:

```
ModularMinimap.ValidateDefaultMaterial
```

which logs `VALIDATE_RESULT: PASS (0 compile errors)`.

> Skipping this step is not fatal. The widgets fall back to drawing the raw map texture unstyled, so
> a missing content step looks ugly rather than broken.

## Step 3 — Give the player vision

Add a **Minimap Revealer** component to your player pawn (Blueprint: Add Component → Minimap
Revealer; C++: `UMinimapRevealerComponent`). Set `RevealRadius` to how far the player should
uncover the map, in world units.

Without a revealer the fog never opens and the map stays dark, so do this before wondering why
nothing shows up.

## Step 4 — Show the minimap

The widgets build their own internal layout, so you do **not** need a Widget Blueprint to see
something on screen:

```cpp
#include "Widgets/MinimapWidget.h"

void AMyHUD::BeginPlay()
{
    Super::BeginPlay();

    if (UMinimapWidget* Minimap = CreateWidget<UMinimapWidget>(GetOwningPlayerController(), UMinimapWidget::StaticClass()))
    {
        Minimap->AddToViewport();
    }
}
```

For real UI, create a Widget Blueprint parented to `MinimapWidget`, size and anchor it in a corner,
and place it in your HUD. Useful properties:

- `MaskShape` — `Circle` or `Rectangle`
- `bRotateWithView` — rotate the map with the camera, or keep it north-up
- `ZoomSteps` / `InitialZoomStep` — world units visible across the widget; call `ZoomIn()` / `ZoomOut()`

Play. You should see walkable area appear as you move around. If the map is empty, jump to
**Troubleshooting**.

## Step 5 — Put things on the map

Add a **Minimap Tracker** component (`UMinimapTrackerComponent`) to any actor that should appear.
Everything lives in its `IconStyle`:

| Property | What it does |
|---|---|
| `Brush` / `Tint` / `Size` | Appearance. Leave the brush empty for a plain dot. |
| `CategoryTag` | Gameplay tag used for grouped show/hide, e.g. `Minimap.Category.Enemy` |
| `EdgeClampMode` | `Hide`, `Clamp`, or `ClampWithArrow` when off-screen |
| `FogRule` | `Always`, `RequireExplored`, or `RequireVisible` |
| `bRotateWithActor` | Rotate the icon to the actor's facing |
| `bShowOnCompass` | Also show it on the compass bar |

Categories are hierarchical — hiding a parent hides its children:

```cpp
UMinimapSubsystem::Get(this)->SetCategoryVisible(EnemyCategoryTag, false);
```

## Step 6 — Add the full-screen map

`UMinimapFullMapWidget` is a `UCommonActivatableWidget`, so push it onto your CommonUI layer stack
the same way you push any other screen. If you are not using a layer stack, `CreateWidget` +
`AddToViewport` works too.

It handles mouse drag to pan, wheel to zoom, and closes on the CommonUI back action. The plugin
ships no input assets on purpose, so bind your own gamepad actions to:

```cpp
FullMap->PanView(FVector2D(StickX, StickY) * PanSpeed * DeltaTime);
FullMap->ZoomView(1.1f);   // > 1 zooms out, < 1 zooms in
FullMap->RecenterView();
```

Clicking an icon fires `OnIconClicked` (and `OnObjectiveClicked` for objectives).

## Step 7 — Objective markers

Objectives are handle-based, so quest code can drive them without owning an actor:

```cpp
UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
const FMinimapIconStyle Style = UMinimapSubsystem::MakeDefaultObjectiveStyle();

// Fixed world position...
FMinimapObjectiveHandle Handle = Minimap->AddObjectiveAtLocation(TargetLocation, Style);

// ...or follow an actor
Handle = Minimap->AddObjectiveOnActor(TargetActor, Style);

Minimap->UpdateObjectiveLocation(Handle, NewLocation);
Minimap->RemoveObjective(Handle);
```

`MakeDefaultObjectiveStyle()` gives you an edge-clamped arrow that ignores fog and shows on the
compass, which is what most quest markers want.

## Step 8 — Compass bar

Add `UMinimapCompassWidget` (or a Widget Blueprint parented to it) anchored across the top of the
screen. It draws cardinal letters plus any marker flagged `bShowOnCompass`, projected by bearing.
`HalfFOVDegrees` controls how much of the world it spans.

North is world **+X**, matching the map's orientation.

By default it follows the player camera's yaw. If your game drives the view some other way — a
spring arm, a top-down controller — implement `IMinimapRotationSource` and register it:

```cpp
UMinimapSubsystem::Get(this)->SetRotationSource(MyRotationProvider);
```

## Step 9 — Persist exploration

The plugin does not own a save file. It hands you bytes; you store them wherever your game already
saves:

```cpp
// Saving
TArray<uint8> FogBlob;
UMinimapSubsystem::Get(this)->ExportFogState(FogBlob);

// Loading
UMinimapSubsystem::Get(this)->ImportFogState(FogBlob);
```

The blob is compressed and carries its own world mapping, so importing still works if the map's
bounds changed since it was written. Import is additive — it reveals, never re-hides.

> `ExportFogState` reads back from the GPU, so call it at save points rather than every frame.

## Step 10 — Turn fog off where you don't want it

Most games want fog in dungeons and none in town. Set `FogMode` on the town's
`UMinimapLevelSettings` asset to **Disabled** and leave the dungeon's on **Inherit** — no code, and
the whole town map shows from the start.

Three layers decide it, highest priority first:

1. A runtime call to `SetFogOfWarEnabled(true/false)` on the subsystem.
2. The active level settings' `FogMode` (`Inherit` defers to the project setting).
3. Project Settings → Plugins → Modular Minimap → `bEnableFogOfWar`.

`ClearFogOfWarOverride()` drops a runtime override and falls back to the level/project settings;
`SetLevelSettings` clears it too, since new settings describe a new area.

```cpp
UMinimapSubsystem* Minimap = UMinimapSubsystem::Get(this);
Minimap->SetFogOfWarEnabled(false);   // e.g. the player used a "Map of the Area" item
```

Turning fog off does not erase exploration, so turning it back on picks up where it left off. While
off, the map draws unfogged and every fog query answers true — `RequireExplored` and
`RequireVisible` icons all show. A level that never turns fog on never allocates the fog textures,
so a fog-free town costs nothing.

`IsFogOfWarEnabled()` reports the current state, and the `OnFogEnabledChanged` event lets your own
widgets react. To check it in-game: `ModularMinimap.SetFogEnabled 0` (or `1`, or `reset`).

## Step 11 — Control the look and the map source

**Per level.** Create a `UMinimapLevelSettings` data asset for fill colour, outline colour and
thickness. To pin the map to an exact area, either place a `AMinimapBoundsVolume` in the level
(and point it at your settings asset) or tick `bOverrideBounds`. Otherwise bounds resolve
automatically from your navigation bounds and grow if needed.

**Authored maps.** Set `BackgroundTexture` and `BackgroundWorldBounds` on the settings asset and the
plugin uses your artwork instead of generating one. Fog, icons and objectives keep working.

**Baking a static level.** **Tools → Bake Minimap Background** renders the level's complete navmesh
to a texture asset and writes a matching settings asset. This needs a fully built navmesh, so it
suits hand-built levels; procedurally generated ones use the runtime path.

**Project-wide tuning.** Project Settings → Plugins → Modular Minimap: texture sizes, tiles stamped
per frame, poll interval, fog update rate and the explored dim factor.

**Your own material.** Point `MapMaterial` at any material honouring the parameter contract in the
README.

---

## Troubleshooting

**The map is empty.** The map is generated from the navigation mesh, so no navmesh means no map.
Check a NavMeshBoundsVolume covers the area and that navigation is built. Then run:

```
ModularMinimap.DumpCoverage
```

It logs how many tiles were captured and exports the raw map to
`Saved/ModularMinimap/Coverage.png`, which tells you whether the problem is capture or display.
Watch the log for `Recreating dtNavMesh instance ... maxTiles` — that means the engine discarded
your baked navmesh at load and you need to rebuild and resave navigation.

**The map only covers where I have walked.** Expected if your project uses navigation invokers
(`bGenerateNavigationOnlyAroundNavigationInvokers`); the navmesh only exists near invokers. The
capture accumulates, so the map keeps everything it has seen. For a complete map up front, bake it
(Step 11) or raise the invoker radius.

**Everything is dark.** No revealer component on the pawn (Step 3), or fog is doing exactly its job.
Adjust `ExploredDimFactor`, or turn fog off for that level (Step 10).

**Icons sit at the edge instead of vanishing.** That is `EdgeClampMode`. Use `Hide` if you do not
want off-screen markers pinned to the border.

**The map looks flat and grey.** The generated material is missing — run Step 2.

**Nothing appears on a dedicated server.** By design: the subsystem is client-only.
