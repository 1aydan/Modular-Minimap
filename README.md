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
- **Fog of war** — unexplored/explored/currently-visible tiers, revealer components, per-level and
  runtime on/off (fogged dungeon, fog-free town), and a compressed-bytes
  `ExportFogState`/`ImportFogState` API so any save system can persist exploration
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

## Fog on/off per level

Fog resolves from three layers, highest priority first:

| Layer | Where |
| --- | --- |
| Runtime override | `SetFogOfWarEnabled(bool)` / `ClearFogOfWarOverride()` on the subsystem |
| Per level | `UMinimapLevelSettings.FogMode` — `Inherit` / `Enabled` / `Disabled` |
| Project | Project Settings → Plugins → Modular Minimap → `bEnableFogOfWar` |

So a town's level settings asset sets `FogMode = Disabled` and a dungeon's leaves `Inherit`, with no
code involved. `IsFogOfWarEnabled()` reports the resolved state and `OnFogEnabledChanged` fires on
every change; map widgets rebind their material automatically.

With fog off the map draws unfogged and every `IsWorldExplored`/`IsWorldVisible` query answers true,
so `RequireExplored`/`RequireVisible` icons all show. Toggling off keeps what has been explored, so
turning fog back on resumes rather than resets — a "reveals the map" item is just
`SetFogOfWarEnabled(false)`. A level that never enables fog never allocates the fog render targets.

Console: `ModularMinimap.SetFogEnabled 0|1|reset`.

## Fog persistence

```cpp
TArray<uint8> Blob;
UMinimapSubsystem::Get(this)->ExportFogState(Blob);   // store Blob in your save game
// ... later ...
UMinimapSubsystem::Get(this)->ImportFogState(Blob);   // additive; safe across bounds changes
```

## Styling material contract

`UMinimapWidgetBase.MapMaterial` (defaults to `/ModularMinimap/Materials/M_MinimapBase`) receives:
textures `MapTexture`, `FogExplored`, `FogVisible`; vectors `CenterUV` (RG), `AspectScale` (RG),
`FillColor`, `OutlineColor`, `GlowColor`; scalars `UVSpan`, `RotationRad`, `MaskShape`
(0 rect / 1 circle), `MapTexelCount`, `ExploredDim`, `FogEnabled`, `BackgroundIsMask` (1 = coverage
mask styled by the material, 0 = authored full-color texture). Swap in your own material honoring
the same parameters for a different look.

The generated material finds the walkable edge by blurring the coverage mask into a ramp and
inverting that ramp back into a signed distance to the boundary, rather than dilating the mask by a
texel count. Every band is a `smoothstep` on that distance in **screen pixels**, so outlines stay
smooth on diagonals and hold their width as the view zooms. The screen-space knobs live on
`UMinimapLevelSettings` and feed the scalars of the same name: `OutlineWidthPixels`,
`OutlineSoftnessPixels`, `OutlineOffsetPixels` (bias the band inside/outside the boundary),
`FillFeatherPixels` and `GlowWidthPixels` (0 disables the glow).

Outline sharpness is ultimately bounded by the coverage texture: the walkable mask is stamped as
hard triangles into a `CoverageTextureSize`-square render target (1024 by default), so at high zoom
the boundary can only be as precise as one texel of that grid. Raising `CoverageTextureSize` to 2048
is the cheapest way to buy more detail.

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
