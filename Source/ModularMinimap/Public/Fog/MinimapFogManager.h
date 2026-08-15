// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MinimapTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectKey.h"
#include "MinimapFogManager.generated.h"

class UMinimapRevealerComponent;
class UMinimapSubsystem;
class UTexture2D;
class UTextureRenderTarget2D;

/**
 * Fog-of-war state for one world. Maintains two R8 render targets:
 *  - ExploredRT: accumulates soft radial reveal stamps from revealer components (never cleared),
 *  - VisibleRT: cleared and restamped each fog update with current vision circles.
 * A widget material composites them: unexplored hidden, explored dim, visible bright.
 *
 * The subsystem owns the revealer registry and gates updates on IsFogOfWarEnabled, so this object is
 * created on the first enable and then kept: exploration survives fog being toggled off and on.
 *
 * Persistence: ExportState packs the explored mask (plus its world mapping) into a zlib-compressed
 * blob; ImportState re-applies a blob additively, remapping through the current projection.
 * A CPU-side stamp history allows lossless redraw when map bounds grow, and a coarse grid answers
 * gameplay-side IsExplored queries without touching the GPU.
 */
UCLASS()
class MODULARMINIMAP_API UMinimapFogManager : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UMinimapSubsystem* InOwner);
	void Deinitialize();

	/** Called from the subsystem tick. Throttles internally to the configured fog update interval. */
	void Update(float DeltaTime);

	/** Clears both targets and redraws exploration from the stamp history (bounds growth). */
	void HandleProjectionChanged();

	/** Skip the remaining update interval so the next Update restamps immediately. */
	void RequestImmediateUpdate() { bForceNextUpdate = true; }

	/** Drops per-revealer bookkeeping when the subsystem unregisters one. */
	void ForgetRevealer(const UMinimapRevealerComponent* Revealer);

	UTextureRenderTarget2D* GetExploredRenderTarget() const { return ExploredRT; }
	UTextureRenderTarget2D* GetVisibleRenderTarget() const { return VisibleRT; }

	/** True once the given world position has been revealed at some point. */
	bool IsWorldExplored(const FVector& WorldLocation) const;

	/** True while the given world position is inside any enabled revealer's radius. */
	bool IsWorldVisible(const FVector& WorldLocation) const;

	/** Pack the explored mask into a compressed blob. Returns false when there is nothing to export. */
	bool ExportState(TArray<uint8>& OutData);

	/** Additively re-apply a previously exported blob. Safe across differing map bounds. */
	bool ImportState(const TArray<uint8>& Data);

private:
	struct FRevealStamp
	{
		FVector2D WorldPos = FVector2D::ZeroVector;
		float Radius = 0.0f;
	};

	void EnsureRenderTargets();
	void EnsureRevealSprite();
	void StampExplored(const TArray<FRevealStamp>& Stamps);
	void StampVisible();
	void MarkGridExplored(const FVector2D& WorldPos, float Radius);
	void RebuildGridFromHistory();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ExploredRT;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> VisibleRT;

	/** Generated soft radial sprite used for all reveal stamps. */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> RevealSprite;

	TWeakObjectPtr<UMinimapSubsystem> Owner;

	/** Last explored-stamp position per revealer, for movement gating. */
	TMap<FObjectKey, FVector2D> LastStampPositions;

	/** Every explored stamp ever applied, for redraws after projection changes. */
	TArray<FRevealStamp> StampHistory;

	/** Coarse explored grid for CPU-side queries; GridSize x GridSize cells over the map square. */
	TArray<uint8> ExploredGrid;
	static constexpr int32 GridSize = 128;

	float UpdateAccumulator = 0.0f;

	/** Set by RequestImmediateUpdate; bypasses the interval once. */
	bool bForceNextUpdate = false;
};
