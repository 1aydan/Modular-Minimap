// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MinimapTypes.h"
#include "NavMesh/RecastNavMesh.h"

class UTextureRenderTarget2D;
class UWorld;
struct FCanvasUVTri;

/** Cached, already-flattened walkable triangles for one navmesh tile, in world-space XY. */
struct FMinimapTileGeometry
{
	/** Triangle vertex triplets in world XY. */
	TArray<FVector2D> TriangleVerts;
};

/**
 * Incremental navmesh-to-texture capture. Detects new or regenerated navmesh tiles by diffing the
 * live tile set against everything seen so far, caches each tile's walkable triangles in world
 * space, and stamps them into a coverage render target. Accumulate-only: tiles the generator later
 * removes (e.g. behind a navigation invoker) stay on the map, and the cache allows a full redraw
 * whenever the map projection changes.
 *
 * Runtime (invoker-driven) generation needs care: while the generator restricts building to its
 * active tile set, ARecastNavMesh's per-tile debug gathering silently yields nothing for tiles
 * outside that set even though those tiles exist and carry polys. Tiles are therefore only queued
 * and gathered while they are in the active set, and a tile that leaves the set before it could be
 * stamped is forgotten rather than recorded, so a later diff can retry it.
 */
class MODULARMINIMAP_API FMinimapNavmeshCapture
{
public:
	void Reset();

	/** Queue any gatherable live tiles not seen before (or regenerated since last seen). Returns number queued. */
	int32 DiffTiles(const ARecastNavMesh& NavMesh);

	/** Union of the world bounds of tiles queued since the last ResetPendingBounds. */
	const FBox2D& GetPendingBounds() const { return PendingBounds; }
	void ResetPendingBounds() { PendingBounds = FBox2D(ForceInit); }

	/** Gather geometry for up to MaxTiles queued tiles and stamp them into the render target. */
	int32 StampPendingTiles(UWorld& World, const ARecastNavMesh& NavMesh, UTextureRenderTarget2D& RenderTarget, const FMinimapProjection& Projection, int32 MaxTiles);

	/** Clear the render target and restamp every cached tile using the given projection. */
	void RedrawAll(UWorld& World, UTextureRenderTarget2D& RenderTarget, const FMinimapProjection& Projection);

	bool HasPendingTiles() const { return PendingCursor < PendingTiles.Num(); }
	int32 GetNumCachedTiles() const { return TileCache.Num(); }
	int32 GetNumPendingTiles() const { return PendingTiles.Num() - PendingCursor; }

	/**
	 * True while the generator only builds (and only exposes debug geometry for) its active tile
	 * set, i.e. navmesh generation is invoker-driven rather than a static level-wide build.
	 */
	static bool IsRestrictedToActiveTiles(const ARecastNavMesh& NavMesh);

private:
	/** A tile awaiting geometry gathering, with the grid cell it occupied when queued. */
	struct FPendingTile
	{
		FNavTileRef Ref;
		FIntVector Cell = FIntVector::ZeroValue;
	};

	void AppendTileTriangles(const FMinimapTileGeometry& Tile, const FMinimapProjection& Projection, const FIntPoint& TextureSize, TArray<FCanvasUVTri>& OutTriangles) const;
	static void DrawTriangles(UWorld& World, UTextureRenderTarget2D& RenderTarget, TArray<FCanvasUVTri>&& Triangles);

	/** Drop the seen record for a cell, but only if it still names the ref that failed. */
	void ForgetCell(const FIntVector& Cell, const FNavTileRef& Ref);

	/**
	 * Most recent tile ref captured (or confirmed empty) per tile grid cell (X, Y, Layer). Keyed by
	 * cell rather than by ref so a rebuilt tile — which gets a fresh salt — diffs as new while the
	 * set stays bounded by the number of cells visited instead of growing with every rebuild.
	 */
	TMap<FIntVector, FNavTileRef> SeenTileRefs;

	/** Tiles queued for geometry gathering and stamping, consumed oldest-first from PendingCursor. */
	TArray<FPendingTile> PendingTiles;

	/** Read position into PendingTiles; entries before it are already processed. */
	int32 PendingCursor = 0;

	/** World-space walkable triangles per tile grid cell (X, Y, Layer). */
	TMap<FIntVector, FMinimapTileGeometry> TileCache;

	/** Union of queued tile bounds, consumed by the owner to drive bounds growth. */
	FBox2D PendingBounds = FBox2D(ForceInit);
};
