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
 */
class MODULARMINIMAP_API FMinimapNavmeshCapture
{
public:
	void Reset();

	/** Queue any live tiles not seen before (or regenerated since last seen). Returns number queued. */
	int32 DiffTiles(const ARecastNavMesh& NavMesh);

	/** Union of the world bounds of tiles queued since the last ResetPendingBounds. */
	const FBox2D& GetPendingBounds() const { return PendingBounds; }
	void ResetPendingBounds() { PendingBounds = FBox2D(ForceInit); }

	/** Gather geometry for up to MaxTiles queued tiles and stamp them into the render target. */
	int32 StampPendingTiles(UWorld& World, const ARecastNavMesh& NavMesh, UTextureRenderTarget2D& RenderTarget, const FMinimapProjection& Projection, int32 MaxTiles);

	/** Clear the render target and restamp every cached tile using the given projection. */
	void RedrawAll(UWorld& World, UTextureRenderTarget2D& RenderTarget, const FMinimapProjection& Projection);

	bool HasPendingTiles() const { return PendingTiles.Num() > 0; }
	int32 GetNumCachedTiles() const { return TileCache.Num(); }
	int32 GetNumPendingTiles() const { return PendingTiles.Num(); }

private:
	void AppendTileTriangles(const FMinimapTileGeometry& Tile, const FMinimapProjection& Projection, const FIntPoint& TextureSize, TArray<FCanvasUVTri>& OutTriangles) const;
	static void DrawTriangles(UWorld& World, UTextureRenderTarget2D& RenderTarget, TArray<FCanvasUVTri>&& Triangles);

	/** Every tile ref ever queued. Refs carry a salt, so regenerated tiles diff as new. */
	TSet<FNavTileRef> SeenTileRefs;

	/** Tiles queued for geometry gathering and stamping. */
	TArray<FNavTileRef> PendingTiles;

	/** World-space walkable triangles per tile grid cell (X, Y, Layer). */
	TMap<FIntVector, FMinimapTileGeometry> TileCache;

	/** Union of queued tile bounds, consumed by the owner to drive bounds growth. */
	FBox2D PendingBounds = FBox2D(ForceInit);
};
