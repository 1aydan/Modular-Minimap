// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Capture/MinimapNavmeshCapture.h"
#include "Interfaces/MinimapRotationSource.h"
#include "MinimapTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ScriptInterface.h"
#include "MinimapSubsystem.generated.h"

class ANavigationData;
class ARecastNavMesh;
class UMinimapFogManager;
class UMinimapLevelSettings;
class UMinimapTrackerComponent;
class UTexture;
class UTexture2D;
class UTextureRenderTarget2D;

/** One registered objective marker: either a fixed world location or a tracked actor. */
USTRUCT()
struct FMinimapObjectiveEntry
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Serial = 0;

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	FMinimapIconStyle Style;

	UPROPERTY()
	FVector FixedLocation = FVector::ZeroVector;

	UPROPERTY()
	TWeakObjectPtr<AActor> TrackedActor;
};

/**
 * World subsystem owning the minimap state for the current level: map projection and bounds,
 * the navmesh capture pipeline, fog of war, and the tracker/objective registries. Widgets and
 * game code talk to this as the single API surface of the plugin.
 */
UCLASS()
class MODULARMINIMAP_API UMinimapSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMinimapSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem interface

	//~ Begin FTickableGameObject interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }
	//~ End FTickableGameObject interface

	/** Broadcast after the map projection changed (initial resolve or bounds growth). */
	UPROPERTY(BlueprintAssignable, Category = "Minimap")
	FOnMinimapBoundsChanged OnBoundsChanged;

	/** Broadcast when the background texture object changed. */
	UPROPERTY(BlueprintAssignable, Category = "Minimap")
	FOnMinimapMapTextureChanged OnMapTextureChanged;

	/** Broadcast when an objective marker is clicked on any map view. */
	UPROPERTY(BlueprintAssignable, Category = "Minimap")
	FOnMinimapObjectiveClicked OnObjectiveClicked;

	/** Current world-to-UV projection for the map. Invalid until bounds are resolved. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	FMinimapProjection GetProjection() const { return Projection; }

	/** Auto-generated walkable-coverage texture. Null until created, or when an authored override is active. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UTextureRenderTarget2D* GetCoverageRenderTarget() const { return CoverageRenderTarget; }

	/** The texture widgets should display: the authored override when set, else the generated coverage. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UTexture* GetBackgroundTexture() const;

	/** Per-level settings resolved for this world, if any. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	UMinimapLevelSettings* GetLevelSettings() const { return ActiveLevelSettings; }

	/** Assign level settings at runtime; re-resolves the background override and map bounds. */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void SetLevelSettings(UMinimapLevelSettings* InSettings);

	/** Explored-area fog mask, or null while fog is disabled/uninitialized. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Fog")
	UTextureRenderTarget2D* GetFogExploredRenderTarget() const;

	/** Current-vision fog mask, or null while fog is disabled/uninitialized. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Fog")
	UTextureRenderTarget2D* GetFogVisibleRenderTarget() const;

	/** True once the world position has been revealed (always true while fog is disabled). */
	UFUNCTION(BlueprintPure, Category = "Minimap|Fog")
	bool IsWorldExplored(const FVector& WorldLocation) const;

	/** True while the world position is inside any revealer's radius (always true while fog is disabled). */
	UFUNCTION(BlueprintPure, Category = "Minimap|Fog")
	bool IsWorldVisible(const FVector& WorldLocation) const;

	/** Pack the current fog exploration into a compressed blob for a save system to store. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Fog")
	bool ExportFogState(TArray<uint8>& OutData);

	/** Re-apply a previously exported fog blob (additive; safe across map bounds changes). */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Fog")
	bool ImportFogState(const TArray<uint8>& Data);

	/** Show or hide a whole icon category (hierarchical: hiding Minimap.Category.Enemy hides Enemy.Elite too). */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void SetCategoryVisible(FGameplayTag Category, bool bVisible);

	/** True unless the tag or any of its ancestors has been hidden. Empty tags are always visible. */
	UFUNCTION(BlueprintPure, Category = "Minimap")
	bool IsCategoryVisible(FGameplayTag Category) const;

	/** Add an objective marker at a fixed world location. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Objectives")
	FMinimapObjectiveHandle AddObjectiveAtLocation(FVector WorldLocation, FMinimapIconStyle Style);

	/** Add an objective marker that follows an actor. Auto-deactivates when the actor is destroyed. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Objectives")
	FMinimapObjectiveHandle AddObjectiveOnActor(AActor* TrackedActor, FMinimapIconStyle Style);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Objectives")
	bool UpdateObjectiveLocation(FMinimapObjectiveHandle Handle, FVector NewLocation);

	UFUNCTION(BlueprintCallable, Category = "Minimap|Objectives")
	bool RemoveObjective(FMinimapObjectiveHandle Handle);

	UFUNCTION(BlueprintPure, Category = "Minimap|Objectives")
	bool IsObjectiveActive(FMinimapObjectiveHandle Handle) const;

	/** Convenience style for objectives: edge-clamped arrow, ignores fog, shown on the compass. */
	UFUNCTION(BlueprintPure, Category = "Minimap|Objectives")
	static FMinimapIconStyle MakeDefaultObjectiveStyle();

	/** Override where map/compass "forward" comes from (e.g. a spring-arm camera yaw provider). */
	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void SetRotationSource(TScriptInterface<IMinimapRotationSource> InSource) { RotationSource = InSource; }

	/** Log capture stats and export the coverage target to Saved/ModularMinimap/Coverage.png. */
	UFUNCTION(BlueprintCallable, Category = "Minimap|Debug")
	void DumpCoverageToSaved();

	/** Register an actor's tracker so its icon shows on map views. Called by the tracker component. */
	void RegisterTracker(UMinimapTrackerComponent* Tracker);
	void UnregisterTracker(UMinimapTrackerComponent* Tracker);

	/** Live tracker list for icon painting. Entries may be stale weak pointers; callers must check. */
	const TArray<TWeakObjectPtr<UMinimapTrackerComponent>>& GetTrackedComponents() const { return TrackedComponents; }

	/** Fog-of-war manager; null when fog is disabled in the developer settings. */
	UMinimapFogManager* GetFogManager() const { return FogManager; }

	/** Load one of the plugin's generated icon textures (e.g. TX_PlayerArrow); null when absent. */
	static UTexture2D* LoadDefaultIconTexture(const TCHAR* TextureName);

	/** Live objective list for icon painting. */
	const TArray<FMinimapObjectiveEntry>& GetObjectives() const { return Objectives; }

	/** Resolve an entry's current world location. Returns false when a tracked actor is gone. */
	static bool ResolveObjectiveLocation(const FMinimapObjectiveEntry& Entry, FVector& OutLocation);

	/** Called by icon layers when an objective marker is clicked. */
	void NotifyObjectiveClicked(FMinimapObjectiveHandle Handle) { OnObjectiveClicked.Broadcast(Handle); }

	/** Yaw in degrees treated as map-forward: the registered source, else the player's camera yaw. */
	float GetViewYawDegrees(const APlayerController* PlayerController) const;

	int32 GetNumCachedTiles() const { return Capture.GetNumCachedTiles(); }
	int32 GetNumPendingTiles() const { return Capture.GetNumPendingTiles(); }

protected:
	UFUNCTION()
	void HandleNavigationGenerationFinished(ANavigationData* NavData);

	void ResolveLevelConfig(UWorld& InWorld);
	void ApplyLevelSettings();
	void ResolveInitialBounds(UWorld& InWorld);
	void EnsureCoverageRenderTarget();
	void HandleBoundsGrowth();
	void UpdateCapture(float DeltaTime);
	ARecastNavMesh* GetRecastNavMesh() const;

	static FBox2D InflateBounds(const FBox2D& Bounds, float Fraction);

	/** World-to-UV mapping shared by the capture pipeline, fog and all widgets. */
	FMinimapProjection Projection;

	/** Incremental navmesh tile capture and CPU-side tile geometry cache. */
	FMinimapNavmeshCapture Capture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CoverageRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMinimapLevelSettings> ActiveLevelSettings;

	UPROPERTY(Transient)
	TObjectPtr<UMinimapFogManager> FogManager;

	/** True when bounds came from an authored source and must not auto-grow. */
	bool bBoundsLocked = false;

	/** True while navmesh capture drives the background (no authored override). */
	bool bCaptureEnabled = true;

	bool bDiffRequested = false;
	bool bAutoDumpDone = false;
	float PollAccumulator = 0.0f;

	/** Registered actor trackers. */
	TArray<TWeakObjectPtr<UMinimapTrackerComponent>> TrackedComponents;

	/** Explicitly hidden (false) or shown (true) categories; unset categories default to visible. */
	TMap<FGameplayTag, bool> CategoryVisibility;

	/** Objective registry; slots are reused, identity = index + serial. */
	UPROPERTY(Transient)
	TArray<FMinimapObjectiveEntry> Objectives;

	int32 NextObjectiveSerial = 1;

	UPROPERTY(Transient)
	TScriptInterface<IMinimapRotationSource> RotationSource;
};
