# Modular Minimap

A standalone Unreal Engine 5.8 C++ plugin providing a navigation-driven minimap system for RPGs.

Instead of requiring hand-authored map textures, the map background is **auto-generated from the
navigation mesh** — walkable space is stamped into a render target as navmesh tiles are generated,
so the map draws itself as the player explores. This works with invoker-based dynamic navmesh
generation and procedurally generated levels (the map accumulates and never erases). Hand-authored
background textures are supported as an optional per-level override.

## Features

- **Auto-generated map** from the navmesh, styled Path-of-Exile-like (dark walkable fill + light outline)
- **Corner minimap** (`UMinimapWidget`) — circular/rectangular mask, rotate-with-view, zoom steps
- **Full-screen map overlay** (`UMinimapFullMapWidget`, a `UCommonActivatableWidget`) — drag pan,
  wheel zoom, recenter, CommonUI back action closes; gamepad via `PanView`/`ZoomView` BlueprintCallables
- **Fog of war** — unexplored/explored/currently-visible tiers, revealer components, and a
  compressed-bytes `ExportFogState`/`ImportFogState` API so any save system can persist exploration
- **Markers** — `UMinimapTrackerComponent` on any actor: brush, tint, gameplay-tag category
  (hierarchical show/hide), edge clamping, fog visibility rules, click events
- **Objective markers** — handle-based `AddObjectiveAtLocation`/`AddObjectiveOnActor` API with
  edge-clamped pointer arrows
- **Compass bar** (`UMinimapCompassWidget`) — cardinal letters + compass-flagged markers by bearing
- **CommonUI bases** throughout (`UCommonUserWidget` / `UCommonActivatableWidget`)
- **Zero-asset default**: widgets auto-build their tree and fall back to unstyled drawing if the
  plugin content has not been generated; generated default assets provide the polished look

## Requirements

Unreal Engine 5.8 · CommonUI (engine plugin, declared as dependency)

## Documentation

**New here? Start with [TUTORIAL.md](TUTORIAL.md)** — a step-by-step walkthrough from installing the
plugin to a working minimap, fog of war, markers, objectives and a compass, with a troubleshooting
section. The rest of this README is the condensed reference.

## Quick start

1. Put the plugin in your project's `Plugins/` folder (folder name `ModularMinimap`).
2. In-editor, run **Tools → Modular Minimap → Create Default Minimap Assets** once (or console
   `ModularMinimap.CreateDefaultAssets`). This generates `M_MinimapBase` and the default icon
   textures into `/ModularMinimap`; commit them.
3. Add a `UMinimapRevealerComponent` to your player pawn (fog vision).
4. Add `UMinimapWidget` to your HUD (a plain `CreateWidget` of the C++ class already works — no
   widget blueprint required; make one deriving from it to skin it).
5. Push `UMinimapFullMapWidget` onto your CommonUI layer stack from your map-open input action.
6. Add `UMinimapTrackerComponent` to anything that should appear on the map.

The subsystem (`UMinimapSubsystem`, a world subsystem) does the rest: it resolves map bounds
(`AMinimapBoundsVolume` → `UMinimapLevelSettings` → navigation bounds → auto-grow from tiles),
captures navmesh tiles incrementally, and runs fog.

## Map source options (per level)

| Setup | Result |
|---|---|
| Nothing | Bounds auto-resolve from nav bounds volumes; map generates as tiles appear |
| `AMinimapBoundsVolume` placed | Bounds locked to the volume's XY footprint |
| `UMinimapLevelSettings` with `BoundsOverride` | Bounds locked to the override |
| `UMinimapLevelSettings` with `BackgroundTexture` | Authored texture shown; navmesh capture off; fog/icons still run |
| **Tools → Bake Minimap Background** | Bakes the current level's full navmesh into a texture + settings asset (static levels only — invoker-generated navmesh can't bake in-editor) |

## Fog persistence

```cpp
TArray<uint8> Blob;
UMinimapSubsystem::Get(this)->ExportFogState(Blob);   // store Blob in your save game
// ... later ...
UMinimapSubsystem::Get(this)->ImportFogState(Blob);   // additive; safe across bounds changes
```

## Styling material contract

`UMinimapWidgetBase.MapMaterial` (defaults to `/ModularMinimap/Materials/M_MinimapBase`) receives:
textures `MapTexture`, `FogExplored`, `FogVisible`; vectors `CenterUV` (RG), `FillColor`,
`OutlineColor`; scalars `UVSpan`, `RotationRad`, `MaskShape` (0 rect / 1 circle), `MapTexelCount`,
`OutlineTexels`, `ExploredDim`, `FogEnabled`, `BackgroundIsMask` (1 = coverage mask styled by the
material, 0 = authored full-color texture). Swap in your own material honoring the same parameters
for a different look.

## DarkTower integration notes (game-side)

- **HUD minimap**: add a `BindWidgetOptional` slot for a `UMinimapWidget` subclass in `UDTHudWidget` / `WBP_HUD`.
- **Full map**: `ADTPlayerController::ToggleWidgetOnLayer<UMinimapFullMapWidget>` on `UI.Layer.Game`;
  `IA_Action_OpenMap` already exists.
- **Rotation source**: implement `IMinimapRotationSource` returning the RPGCamera spring-arm yaw and
  register with `UMinimapSubsystem::SetRotationSource`.
- **Objectives**: subscribe `UQuestSubsystem::OnQuestStateChanged` / `OnQuestObjectiveProgress` in a
  game-side adapter, map objective IDs to world locations, and drive
  `AddObjectiveAtLocation`/`RemoveObjective` (quest objectives carry no world position themselves).
- **Fog saves**: store the fog blob per level (e.g. keyed by dungeon seed + floor) in `UDTSaveGame`
  via the `ISaveable` seam.

## Development

Developed against a host project via directory junction (folder name must be `ModularMinimap`):

```bat
mklink /J "<Project>\Plugins\ModularMinimap" "<this repo>"
"<UE_5.8>\Engine\Build\BatchFiles\Build.bat" <Project>Editor Win64 Development -project="<Project>.uproject" -WaitMutex
```

Standalone validation/packaging:

```bat
"<UE_5.8>\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="<this repo>\ModularMinimap.uplugin" -Package="<out>" -TargetPlatforms=Win64
```

Debug console commands: `ModularMinimap.DumpCoverage` (stats + PNG export of the coverage target).

## Known limitations (v1)

- Multi-floor geometry projects onto one plane (per-floor layers are future work).
- Fog export does a synchronous GPU readback (~0.25 MB) — call it at save points.
- The in-editor bake needs a full navmesh; invoker-based projects use the runtime pipeline instead.
