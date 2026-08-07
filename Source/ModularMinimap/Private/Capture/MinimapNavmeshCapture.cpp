// Copyright Epic Games, Inc. All Rights Reserved.

#include "Capture/MinimapNavmeshCapture.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GlobalRenderResources.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "ModularMinimapModule.h"

#if WITH_RECAST
#include "NavMesh/RecastNavMeshGenerator.h"
#endif

/** Number of consumed entries tolerated at the head of the pending queue before compacting. */
static constexpr int32 GPendingCompactThreshold = 256;

bool FMinimapNavmeshCapture::IsRestrictedToActiveTiles(const ARecastNavMesh& NavMesh)
{
#if WITH_RECAST
	// Mirrors the guard inside FPImplRecastNavMesh::GetDebugGeometryForTile. GetActiveTileSet()
	// asserts on a missing generator, so the null check has to come first.
	const FRecastNavMeshGenerator* Generator = static_cast<const FRecastNavMeshGenerator*>(NavMesh.GetGenerator());
	return Generator != nullptr
		&& Generator->IsBuildingRestrictedToActiveTiles()
		&& !NavMesh.GetActiveTileSet().IsEmpty();
#else
	return false;
#endif
}

void FMinimapNavmeshCapture::Reset()
{
	SeenTileRefs.Reset();
	PendingTiles.Reset();
	PendingCursor = 0;
	TileCache.Reset();
	PendingBounds = FBox2D(ForceInit);
}

void FMinimapNavmeshCapture::ForgetCell(const FIntVector& Cell, const FNavTileRef& Ref)
{
	// A newer ref for the same cell may already have been queued; only the failing one is forgotten.
	const FNavTileRef* SeenRef = SeenTileRefs.Find(Cell);
	if (SeenRef != nullptr && *SeenRef == Ref)
	{
		SeenTileRefs.Remove(Cell);
	}
}

int32 FMinimapNavmeshCapture::DiffTiles(const ARecastNavMesh& NavMesh)
{
	TArray<FNavTileRef> LiveTiles;
	NavMesh.GetAllNavMeshTiles(LiveTiles);

	const bool bRestricted = IsRestrictedToActiveTiles(NavMesh);
	const TSet<FIntPoint>* ActiveTiles = bRestricted ? &NavMesh.GetActiveTileSet() : nullptr;

	int32 NumQueued = 0;
	for (const FNavTileRef& TileRef : LiveTiles)
	{
		if (!TileRef.IsValid())
		{
			continue;
		}

		// GetAllNavMeshTiles yields a ref for every tile *slot* the navmesh allocated, including
		// unused ones. Those have no header, so the coordinate lookup fails and they are skipped
		// without being marked seen: the same slot becomes a real tile once generation fills it in.
		int32 TileX = 0;
		int32 TileY = 0;
		int32 TileLayer = 0;
		if (!NavMesh.GetNavMeshTileXY(TileRef, TileX, TileY, TileLayer))
		{
			continue;
		}

		const FIntVector Cell(TileX, TileY, TileLayer);
		const FNavTileRef* SeenRef = SeenTileRefs.Find(Cell);
		if (SeenRef != nullptr && *SeenRef == TileRef)
		{
			continue;
		}

		const FBox TileBounds = NavMesh.GetNavMeshTileBounds(TileRef);
		if (!TileBounds.IsValid)
		{
			continue;
		}

		// While building is restricted to the active tile set, per-tile debug gathering returns
		// nothing for tiles outside it. Queuing those would burn the stamping budget on tiles that
		// cannot produce geometry, so leave them unseen and pick them up if an invoker brings the
		// cell back into the set.
		if (ActiveTiles != nullptr && !ActiveTiles->Contains(FIntPoint(TileX, TileY)))
		{
			continue;
		}

		SeenTileRefs.Add(Cell, TileRef);
		PendingTiles.Add(FPendingTile{TileRef, Cell});
		++NumQueued;

		PendingBounds += FBox2D(
			FVector2D(TileBounds.Min.X, TileBounds.Min.Y),
			FVector2D(TileBounds.Max.X, TileBounds.Max.Y));
	}

	return NumQueued;
}

int32 FMinimapNavmeshCapture::StampPendingTiles(UWorld& World, const ARecastNavMesh& NavMesh, UTextureRenderTarget2D& RenderTarget, const FMinimapProjection& Projection, int32 MaxTiles)
{
	if (!HasPendingTiles() || !Projection.IsValid())
	{
		return 0;
	}

	const bool bRestricted = IsRestrictedToActiveTiles(NavMesh);
	const TSet<FIntPoint>* ActiveTiles = bRestricted ? &NavMesh.GetActiveTileSet() : nullptr;

	const FIntPoint TextureSize(RenderTarget.SizeX, RenderTarget.SizeY);
	TArray<FCanvasUVTri> Triangles;
	int32 NumProcessed = 0;

	// Oldest first: a tile only stays gatherable while it is active, so the entries most at risk of
	// going stale are the ones that have been waiting longest.
	while (NumProcessed < MaxTiles && HasPendingTiles())
	{
		const FPendingTile Pending = PendingTiles[PendingCursor++];
		++NumProcessed;

		// The cell may have dropped out of the active set between queuing and stamping. Gathering
		// now would silently return nothing, so forget the cell instead of recording a miss and let
		// a later diff re-queue it if the generator brings it back.
		if (ActiveTiles != nullptr && !ActiveTiles->Contains(FIntPoint(Pending.Cell.X, Pending.Cell.Y)))
		{
			ForgetCell(Pending.Cell, Pending.Ref);
			continue;
		}

		// The return value reports whether collection finished for *all* tiles; when asking for one
		// specific tile it is false even on success, so it must not be read as a failure signal.
		FRecastDebugGeometry Geometry;
		NavMesh.GetDebugGeometryForTile(Geometry, Pending.Ref);

		if (Geometry.MeshVerts.IsEmpty())
		{
			// The tile is active but exposes no walkable geometry, or was rebuilt under a fresh salt
			// since it was queued. Either way the cell stays recorded against this ref so it is not
			// retried every poll; a rebuild changes the ref and diffs as new on its own.
			continue;
		}

		FMinimapTileGeometry& Cached = TileCache.FindOrAdd(Pending.Cell);
		Cached.TriangleVerts.Reset();

		for (int32 AreaIndex = 0; AreaIndex < RECAST_MAX_AREAS; ++AreaIndex)
		{
			const TArray<int32>& Indices = Geometry.AreaIndices[AreaIndex];
			for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
			{
				const FVector& V0 = Geometry.MeshVerts[Indices[Index + 0]];
				const FVector& V1 = Geometry.MeshVerts[Indices[Index + 1]];
				const FVector& V2 = Geometry.MeshVerts[Indices[Index + 2]];
				Cached.TriangleVerts.Add(FVector2D(V0.X, V0.Y));
				Cached.TriangleVerts.Add(FVector2D(V1.X, V1.Y));
				Cached.TriangleVerts.Add(FVector2D(V2.X, V2.Y));
			}
		}

		AppendTileTriangles(Cached, Projection, TextureSize, Triangles);
	}

	if (!HasPendingTiles())
	{
		PendingTiles.Reset();
		PendingCursor = 0;
	}
	else if (PendingCursor >= GPendingCompactThreshold)
	{
		PendingTiles.RemoveAt(0, PendingCursor, EAllowShrinking::No);
		PendingCursor = 0;
	}

	if (Triangles.Num() > 0)
	{
		DrawTriangles(World, RenderTarget, MoveTemp(Triangles));
	}

	return NumProcessed;
}

void FMinimapNavmeshCapture::RedrawAll(UWorld& World, UTextureRenderTarget2D& RenderTarget, const FMinimapProjection& Projection)
{
	UKismetRenderingLibrary::ClearRenderTarget2D(&World, &RenderTarget, FLinearColor::Transparent);

	if (!Projection.IsValid() || TileCache.IsEmpty())
	{
		return;
	}

	const FIntPoint TextureSize(RenderTarget.SizeX, RenderTarget.SizeY);
	TArray<FCanvasUVTri> Triangles;
	for (const TPair<FIntVector, FMinimapTileGeometry>& Pair : TileCache)
	{
		AppendTileTriangles(Pair.Value, Projection, TextureSize, Triangles);
	}

	if (Triangles.Num() > 0)
	{
		DrawTriangles(World, RenderTarget, MoveTemp(Triangles));
	}
}

void FMinimapNavmeshCapture::AppendTileTriangles(const FMinimapTileGeometry& Tile, const FMinimapProjection& Projection, const FIntPoint& TextureSize, TArray<FCanvasUVTri>& OutTriangles) const
{
	const int32 NumTriangles = Tile.TriangleVerts.Num() / 3;
	OutTriangles.Reserve(OutTriangles.Num() + NumTriangles);

	for (int32 TriangleIndex = 0; TriangleIndex < NumTriangles; ++TriangleIndex)
	{
		FCanvasUVTri& Triangle = OutTriangles.AddDefaulted_GetRef();
		Triangle.V0_Pos = Projection.World2DToPixel(Tile.TriangleVerts[TriangleIndex * 3 + 0], TextureSize);
		Triangle.V1_Pos = Projection.World2DToPixel(Tile.TriangleVerts[TriangleIndex * 3 + 1], TextureSize);
		Triangle.V2_Pos = Projection.World2DToPixel(Tile.TriangleVerts[TriangleIndex * 3 + 2], TextureSize);
		Triangle.V0_Color = FLinearColor::White;
		Triangle.V1_Color = FLinearColor::White;
		Triangle.V2_Color = FLinearColor::White;
	}
}

void FMinimapNavmeshCapture::DrawTriangles(UWorld& World, UTextureRenderTarget2D& RenderTarget, TArray<FCanvasUVTri>&& Triangles)
{
	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(&World, &RenderTarget, Canvas, CanvasSize, Context);

	if (Canvas != nullptr)
	{
		FCanvasTriangleItem TriangleItem(Triangles, GWhiteTexture);
		TriangleItem.BlendMode = SE_BLEND_Opaque;
		Canvas->DrawItem(TriangleItem);
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(&World, Context);
}
