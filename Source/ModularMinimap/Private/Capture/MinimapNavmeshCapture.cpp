// Copyright Epic Games, Inc. All Rights Reserved.

#include "Capture/MinimapNavmeshCapture.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GlobalRenderResources.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "ModularMinimapModule.h"

void FMinimapNavmeshCapture::Reset()
{
	SeenTileRefs.Reset();
	PendingTiles.Reset();
	TileCache.Reset();
	PendingBounds = FBox2D(ForceInit);
}

int32 FMinimapNavmeshCapture::DiffTiles(const ARecastNavMesh& NavMesh)
{
	TArray<FNavTileRef> LiveTiles;
	NavMesh.GetAllNavMeshTiles(LiveTiles);

	int32 NumQueued = 0;
	for (const FNavTileRef& TileRef : LiveTiles)
	{
		if (!TileRef.IsValid() || SeenTileRefs.Contains(TileRef))
		{
			continue;
		}

		// GetAllNavMeshTiles yields a ref for every tile *slot* the navmesh allocated, including
		// unused ones. Those have no bounds and no geometry, so skip them without marking them
		// seen: the same slot becomes a real tile once generation fills it in.
		const FBox TileBounds = NavMesh.GetNavMeshTileBounds(TileRef);
		if (!TileBounds.IsValid)
		{
			continue;
		}

		SeenTileRefs.Add(TileRef);
		PendingTiles.Add(TileRef);
		++NumQueued;

		PendingBounds += FBox2D(
			FVector2D(TileBounds.Min.X, TileBounds.Min.Y),
			FVector2D(TileBounds.Max.X, TileBounds.Max.Y));
	}

	return NumQueued;
}

int32 FMinimapNavmeshCapture::StampPendingTiles(UWorld& World, const ARecastNavMesh& NavMesh, UTextureRenderTarget2D& RenderTarget, const FMinimapProjection& Projection, int32 MaxTiles)
{
	if (PendingTiles.IsEmpty() || !Projection.IsValid())
	{
		return 0;
	}

	const FIntPoint TextureSize(RenderTarget.SizeX, RenderTarget.SizeY);
	TArray<FCanvasUVTri> Triangles;
	int32 NumProcessed = 0;

	while (NumProcessed < MaxTiles && !PendingTiles.IsEmpty())
	{
		const FNavTileRef TileRef = PendingTiles.Pop(EAllowShrinking::No);
		++NumProcessed;

		// The return value reports whether collection finished for *all* tiles; when asking for one
		// specific tile it is false even on success, so it must not be read as a failure signal.
		FRecastDebugGeometry Geometry;
		NavMesh.GetDebugGeometryForTile(Geometry, TileRef);

		if (Geometry.MeshVerts.IsEmpty())
		{
			// The tile exists but exposes no walkable geometry right now. With invoker-driven
			// generation this is normal for tiles outside the current active set, so forget the ref
			// and let a later diff retry it once the tile becomes active.
			SeenTileRefs.Remove(TileRef);
			continue;
		}

		int32 TileX = 0;
		int32 TileY = 0;
		int32 TileLayer = 0;
		NavMesh.GetNavMeshTileXY(TileRef, TileX, TileY, TileLayer);

		FMinimapTileGeometry& Cached = TileCache.FindOrAdd(FIntVector(TileX, TileY, TileLayer));
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
