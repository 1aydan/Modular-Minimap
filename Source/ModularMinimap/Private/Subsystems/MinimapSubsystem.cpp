// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/MinimapSubsystem.h"

#include "Actors/MinimapBoundsVolume.h"
#include "Components/MinimapTrackerComponent.h"
#include "Data/MinimapLevelSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Fog/MinimapFogManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ModularMinimapModule.h"
#include "NavigationSystem.h"
#include "Settings/MinimapDeveloperSettings.h"
#include "TimerManager.h"

UMinimapSubsystem* UMinimapSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	return World->GetSubsystem<UMinimapSubsystem>();
}

bool UMinimapSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	// The minimap is a purely client-side presentation system.
	return !IsRunningDedicatedServer();
}

bool UMinimapSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMinimapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMinimapSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	if (World)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (NavSys)
		{
			NavSys->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this, &UMinimapSubsystem::HandleNavigationGenerationFinished);
		}
	}

	if (FogManager != nullptr)
	{
		FogManager->Deinitialize();
		FogManager = nullptr;
	}

	Capture.Reset();
	Super::Deinitialize();
}

void UMinimapSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (GetDefault<UMinimapDeveloperSettings>()->bEnableFogOfWar)
	{
		FogManager = NewObject<UMinimapFogManager>(this);
		FogManager->Initialize(this);
	}

	ResolveLevelConfig(InWorld);

	if (bCaptureEnabled)
	{
		ResolveInitialBounds(InWorld);

		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld);
		if (NavSys)
		{
			NavSys->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this, &UMinimapSubsystem::HandleNavigationGenerationFinished);
		}

		bDiffRequested = true;
	}

	UE_LOG(LogModularMinimap, Log, TEXT("MinimapSubsystem active for world '%s' (capture %s, bounds %s)"),
		*InWorld.GetName(),
		bCaptureEnabled ? TEXT("enabled") : TEXT("disabled: authored background"),
		Projection.IsValid() ? TEXT("resolved") : TEXT("pending"));

}

void UMinimapSubsystem::DumpCoverageToSaved()
{
	UE_LOG(LogModularMinimap, Log, TEXT("Minimap: cached tiles=%d, pending tiles=%d, bounds %s (min=%s max=%s, uv span=%.0f uu)"),
		GetNumCachedTiles(),
		GetNumPendingTiles(),
		Projection.IsValid() ? TEXT("valid") : TEXT("unresolved"),
		*Projection.WorldBounds.Min.ToString(),
		*Projection.WorldBounds.Max.ToString(),
		Projection.WorldSize);

	const ARecastNavMesh* NavMesh = GetRecastNavMesh();
	if (NavMesh)
	{
		TArray<FNavTileRef> LiveTiles;
		NavMesh->GetAllNavMeshTiles(LiveTiles);

		// GetAllNavMeshTiles returns a ref per tile *slot*, including unused ones, so count the
		// slots that actually carry geometry.
		int32 TilesWithBounds = 0;
		for (const FNavTileRef& TileRef : LiveTiles)
		{
			if (NavMesh->GetNavMeshTileBounds(TileRef).IsValid)
			{
				++TilesWithBounds;
			}
		}

		// Gathering with an invalid ref collects every tile at once — or, while generation is
		// restricted to the active tile set, only that set. If this is also empty the navmesh itself
		// has no built geometry rather than the per-tile path being at fault.
		FRecastDebugGeometry AllGeometry;
		NavMesh->GetDebugGeometryForTile(AllGeometry, FNavTileRef());
		int32 TotalTriangles = 0;
		for (int32 AreaIndex = 0; AreaIndex < RECAST_MAX_AREAS; ++AreaIndex)
		{
			TotalTriangles += AllGeometry.AreaIndices[AreaIndex].Num() / 3;
		}

		// GetActiveTileSet asserts when the navmesh has no generator, which is the normal state for
		// a statically built navmesh in a cooked build.
		const bool bRestricted = FMinimapNavmeshCapture::IsRestrictedToActiveTiles(*NavMesh);
		const int32 NumActiveTiles = NavMesh->GetGenerator() != nullptr ? NavMesh->GetActiveTileSet().Num() : 0;

		UE_LOG(LogModularMinimap, Log, TEXT("Minimap: navmesh '%s' tile slots=%d, tiles with bounds=%d, active set=%d (restricted=%s), all-tile gather verts=%d tris=%d"),
			*NavMesh->GetName(), LiveTiles.Num(), TilesWithBounds, NumActiveTiles,
			bRestricted ? TEXT("yes") : TEXT("no"),
			AllGeometry.MeshVerts.Num(), TotalTriangles);
	}
	else
	{
		UE_LOG(LogModularMinimap, Warning, TEXT("Minimap: no recast navmesh registered in this world."));
	}

	UWorld* World = GetWorld();
	if (World != nullptr && CoverageRenderTarget != nullptr)
	{
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("ModularMinimap");
		UKismetRenderingLibrary::ExportRenderTarget(World, CoverageRenderTarget, Directory, TEXT("Coverage.png"));
		UE_LOG(LogModularMinimap, Log, TEXT("Coverage render target exported to %s"), *(Directory / TEXT("Coverage.png")));
	}
}

void UMinimapSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bCaptureEnabled)
	{
		UpdateCapture(DeltaTime);
	}

	if (FogManager != nullptr)
	{
		FogManager->Update(DeltaTime);
	}

	// Headless verification hook: -ExecCmds="ModularMinimap.AutoDumpAfterSeconds 60" dumps coverage
	// N seconds into gameplay. Checked per-tick because -ExecCmds runs after initial world begin play.
	if (!bAutoDumpDone)
	{
		const IConsoleVariable* AutoDumpCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("ModularMinimap.AutoDumpAfterSeconds"));
		const float AutoDumpDelay = AutoDumpCVar != nullptr ? AutoDumpCVar->GetFloat() : 0.0f;
		if (AutoDumpDelay > 0.0f && GetWorld() != nullptr && GetWorld()->GetTimeSeconds() >= AutoDumpDelay)
		{
			bAutoDumpDone = true;
			DumpCoverageToSaved();
		}
	}
}

TStatId UMinimapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMinimapSubsystem, STATGROUP_Tickables);
}

UTexture* UMinimapSubsystem::GetBackgroundTexture() const
{
	if (ActiveLevelSettings != nullptr && ActiveLevelSettings->BackgroundTexture != nullptr)
	{
		return ActiveLevelSettings->BackgroundTexture;
	}

	return CoverageRenderTarget;
}

void UMinimapSubsystem::SetLevelSettings(UMinimapLevelSettings* InSettings)
{
	ActiveLevelSettings = InSettings;
	ApplyLevelSettings();
	OnMapTextureChanged.Broadcast();
}

UTextureRenderTarget2D* UMinimapSubsystem::GetFogExploredRenderTarget() const
{
	return FogManager != nullptr ? FogManager->GetExploredRenderTarget() : nullptr;
}

UTextureRenderTarget2D* UMinimapSubsystem::GetFogVisibleRenderTarget() const
{
	return FogManager != nullptr ? FogManager->GetVisibleRenderTarget() : nullptr;
}

bool UMinimapSubsystem::IsWorldExplored(const FVector& WorldLocation) const
{
	return FogManager == nullptr || FogManager->IsWorldExplored(WorldLocation);
}

bool UMinimapSubsystem::IsWorldVisible(const FVector& WorldLocation) const
{
	return FogManager == nullptr || FogManager->IsWorldVisible(WorldLocation);
}

bool UMinimapSubsystem::ExportFogState(TArray<uint8>& OutData)
{
	return FogManager != nullptr && FogManager->ExportState(OutData);
}

bool UMinimapSubsystem::ImportFogState(const TArray<uint8>& Data)
{
	return FogManager != nullptr && FogManager->ImportState(Data);
}

void UMinimapSubsystem::RegisterTracker(UMinimapTrackerComponent* Tracker)
{
	if (Tracker != nullptr)
	{
		TrackedComponents.AddUnique(Tracker);
	}
}

void UMinimapSubsystem::UnregisterTracker(UMinimapTrackerComponent* Tracker)
{
	TrackedComponents.RemoveAll([Tracker](const TWeakObjectPtr<UMinimapTrackerComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Tracker;
	});
}

FMinimapObjectiveHandle UMinimapSubsystem::AddObjectiveAtLocation(FVector WorldLocation, FMinimapIconStyle Style)
{
	int32 Index = Objectives.IndexOfByPredicate([](const FMinimapObjectiveEntry& Entry) { return !Entry.bActive; });
	if (Index == INDEX_NONE)
	{
		Index = Objectives.AddDefaulted();
	}

	FMinimapObjectiveEntry& Entry = Objectives[Index];
	Entry.Serial = NextObjectiveSerial++;
	Entry.bActive = true;
	Entry.Style = Style;
	Entry.FixedLocation = WorldLocation;
	Entry.TrackedActor.Reset();

	FMinimapObjectiveHandle Handle;
	Handle.Index = Index;
	Handle.Serial = Entry.Serial;
	return Handle;
}

FMinimapObjectiveHandle UMinimapSubsystem::AddObjectiveOnActor(AActor* TrackedActor, FMinimapIconStyle Style)
{
	FMinimapObjectiveHandle Handle = AddObjectiveAtLocation(TrackedActor != nullptr ? TrackedActor->GetActorLocation() : FVector::ZeroVector, Style);
	if (Handle.IsValid())
	{
		Objectives[Handle.Index].TrackedActor = TrackedActor;
	}
	return Handle;
}

bool UMinimapSubsystem::UpdateObjectiveLocation(FMinimapObjectiveHandle Handle, FVector NewLocation)
{
	if (!IsObjectiveActive(Handle))
	{
		return false;
	}

	FMinimapObjectiveEntry& Entry = Objectives[Handle.Index];
	Entry.FixedLocation = NewLocation;
	Entry.TrackedActor.Reset();
	return true;
}

bool UMinimapSubsystem::RemoveObjective(FMinimapObjectiveHandle Handle)
{
	if (!IsObjectiveActive(Handle))
	{
		return false;
	}

	Objectives[Handle.Index].bActive = false;
	Objectives[Handle.Index].TrackedActor.Reset();
	return true;
}

bool UMinimapSubsystem::IsObjectiveActive(FMinimapObjectiveHandle Handle) const
{
	return Handle.IsValid()
		&& Objectives.IsValidIndex(Handle.Index)
		&& Objectives[Handle.Index].bActive
		&& Objectives[Handle.Index].Serial == Handle.Serial;
}

FMinimapIconStyle UMinimapSubsystem::MakeDefaultObjectiveStyle()
{
	FMinimapIconStyle Style;
	Style.Size = FVector2D(18.0, 18.0);
	Style.Tint = FLinearColor(1.0f, 0.85f, 0.3f, 1.0f);
	Style.EdgeClampMode = EMinimapEdgeClampMode::ClampWithArrow;
	Style.FogRule = EMinimapFogRule::Always;
	Style.bShowOnCompass = true;
	Style.ZOrder = 100;

	UTexture2D* MarkerTexture = LoadDefaultIconTexture(TEXT("TX_ObjectiveMarker"));
	if (MarkerTexture)
	{
		Style.Brush.SetResourceObject(MarkerTexture);
	}

	return Style;
}

UTexture2D* UMinimapSubsystem::LoadDefaultIconTexture(const TCHAR* TextureName)
{
	const FString PackagePath = FString(TEXT("/ModularMinimap/Textures/")) + TextureName;
	if (!FPackageName::DoesPackageExist(PackagePath))
	{
		return nullptr;
	}

	return LoadObject<UTexture2D>(nullptr, *(PackagePath + TEXT(".") + TextureName));
}

bool UMinimapSubsystem::ResolveObjectiveLocation(const FMinimapObjectiveEntry& Entry, FVector& OutLocation)
{
	if (Entry.TrackedActor.IsStale())
	{
		return false;
	}

	const AActor* Actor = Entry.TrackedActor.Get();
	if (Actor)
	{
		OutLocation = Actor->GetActorLocation();
		return true;
	}

	OutLocation = Entry.FixedLocation;
	return true;
}

float UMinimapSubsystem::GetViewYawDegrees(const APlayerController* PlayerController) const
{
	if (RotationSource.GetObject() != nullptr)
	{
		return IMinimapRotationSource::Execute_GetMinimapYaw(RotationSource.GetObject());
	}

	if (PlayerController != nullptr)
	{
		const APawn* Pawn = PlayerController->GetPawn();
		if (Pawn != nullptr)
		{
			return Pawn->GetActorRotation().Yaw;
		}
	}

	return 0.0f;
}

void UMinimapSubsystem::SetCategoryVisible(FGameplayTag Category, bool bVisible)
{
	if (Category.IsValid())
	{
		CategoryVisibility.Add(Category, bVisible);
	}
}

bool UMinimapSubsystem::IsCategoryVisible(FGameplayTag Category) const
{
	if (!Category.IsValid())
	{
		return true;
	}

	for (const TPair<FGameplayTag, bool>& Pair : CategoryVisibility)
	{
		if (!Pair.Value && Category.MatchesTag(Pair.Key))
		{
			return false;
		}
	}

	return true;
}

void UMinimapSubsystem::HandleNavigationGenerationFinished(ANavigationData* NavData)
{
	bDiffRequested = true;
}

void UMinimapSubsystem::ResolveLevelConfig(UWorld& InWorld)
{
	for (TActorIterator<AMinimapBoundsVolume> It(&InWorld); It; ++It)
	{
		AMinimapBoundsVolume* Volume = *It;
		if (Volume == nullptr)
		{
			continue;
		}

		if (Volume->LevelSettings != nullptr)
		{
			ActiveLevelSettings = Volume->LevelSettings;
		}

		Projection.SetFromBounds(Volume->GetWorldBounds2D());
		bBoundsLocked = true;
		break;
	}

	ApplyLevelSettings();
}

void UMinimapSubsystem::ApplyLevelSettings()
{
	if (ActiveLevelSettings == nullptr)
	{
		return;
	}

	if (ActiveLevelSettings->BackgroundTexture != nullptr)
	{
		bCaptureEnabled = false;

		if (ActiveLevelSettings->BackgroundWorldBounds.bIsValid)
		{
			Projection.SetFromBounds(ActiveLevelSettings->BackgroundWorldBounds);
			bBoundsLocked = true;
			OnBoundsChanged.Broadcast();
		}
		else
		{
			UE_LOG(LogModularMinimap, Warning, TEXT("Level settings '%s' set a background texture without valid BackgroundWorldBounds."), *ActiveLevelSettings->GetName());
		}
	}
	else if (ActiveLevelSettings->bOverrideBounds && ActiveLevelSettings->BoundsOverride.bIsValid && !bBoundsLocked)
	{
		Projection.SetFromBounds(ActiveLevelSettings->BoundsOverride);
		bBoundsLocked = true;
		OnBoundsChanged.Broadcast();
	}
}

void UMinimapSubsystem::ResolveInitialBounds(UWorld& InWorld)
{
	if (Projection.IsValid())
	{
		EnsureCoverageRenderTarget();
		return;
	}

	// Auto: union of all registered navigation bounds volumes, inflated by the configured margin.
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld);
	if (NavSys)
	{
		FBox2D Union(ForceInit);
		for (const FNavigationBounds& NavBounds : NavSys->GetNavigationBounds())
		{
			if (NavBounds.AreaBox.IsValid)
			{
				Union += FBox2D(
					FVector2D(NavBounds.AreaBox.Min.X, NavBounds.AreaBox.Min.Y),
					FVector2D(NavBounds.AreaBox.Max.X, NavBounds.AreaBox.Max.Y));
			}
		}

		if (Union.bIsValid)
		{
			const UMinimapDeveloperSettings* Settings = GetDefault<UMinimapDeveloperSettings>();
			Projection.SetFromBounds(InflateBounds(Union, Settings->AutoBoundsMargin));
			bBoundsLocked = false;
			OnBoundsChanged.Broadcast();
		}
	}

	if (Projection.IsValid())
	{
		EnsureCoverageRenderTarget();
	}
	// Otherwise bounds resolve lazily from the first stamped tiles in HandleBoundsGrowth.
}

void UMinimapSubsystem::EnsureCoverageRenderTarget()
{
	if (CoverageRenderTarget != nullptr || !bCaptureEnabled)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const UMinimapDeveloperSettings* Settings = GetDefault<UMinimapDeveloperSettings>();
	CoverageRenderTarget = UKismetRenderingLibrary::CreateRenderTarget2D(
		World, Settings->CoverageTextureSize, Settings->CoverageTextureSize, RTF_RGBA8, FLinearColor::Transparent, false, false);

	OnMapTextureChanged.Broadcast();
}

void UMinimapSubsystem::HandleBoundsGrowth()
{
	const FBox2D& Pending = Capture.GetPendingBounds();
	if (!Pending.bIsValid)
	{
		return;
	}

	UWorld* World = GetWorld();
	const UMinimapDeveloperSettings* Settings = GetDefault<UMinimapDeveloperSettings>();

	if (!Projection.IsValid())
	{
		// Lazy first resolve: no volume, no settings, no nav bounds — size to the first tiles seen.
		Projection.SetFromBounds(InflateBounds(Pending, Settings->AutoBoundsMargin));
		bBoundsLocked = false;
		EnsureCoverageRenderTarget();
		Capture.ResetPendingBounds();
		OnBoundsChanged.Broadcast();
		return;
	}

	if (bBoundsLocked)
	{
		Capture.ResetPendingBounds();
		return;
	}

	const double HalfSize = Projection.WorldSize * 0.5;
	const FVector2D SquareMin = Projection.SquareCenter - FVector2D(HalfSize, HalfSize);
	const FVector2D SquareMax = Projection.SquareCenter + FVector2D(HalfSize, HalfSize);
	const bool bContained =
		Pending.Min.X >= SquareMin.X && Pending.Min.Y >= SquareMin.Y &&
		Pending.Max.X <= SquareMax.X && Pending.Max.Y <= SquareMax.Y;

	if (!bContained)
	{
		FBox2D NewBounds = Projection.WorldBounds;
		NewBounds += Pending;
		Projection.SetFromBounds(InflateBounds(NewBounds, Settings->AutoBoundsMargin));

		if (World != nullptr && CoverageRenderTarget != nullptr)
		{
			Capture.RedrawAll(*World, *CoverageRenderTarget, Projection);
		}

		if (FogManager != nullptr)
		{
			FogManager->HandleProjectionChanged();
		}

		UE_LOG(LogModularMinimap, Log, TEXT("Minimap bounds grew to min=%s max=%s"),
			*Projection.WorldBounds.Min.ToString(), *Projection.WorldBounds.Max.ToString());
		OnBoundsChanged.Broadcast();
	}

	Capture.ResetPendingBounds();
}

void UMinimapSubsystem::UpdateCapture(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const UMinimapDeveloperSettings* Settings = GetDefault<UMinimapDeveloperSettings>();

	PollAccumulator += DeltaTime;
	if (PollAccumulator >= Settings->TilePollInterval)
	{
		PollAccumulator = 0.0f;
		bDiffRequested = true;
	}

	ARecastNavMesh* NavMesh = GetRecastNavMesh();
	if (NavMesh == nullptr)
	{
		return;
	}

	if (bDiffRequested)
	{
		bDiffRequested = false;
		Capture.DiffTiles(*NavMesh);
	}

	HandleBoundsGrowth();

	if (!Projection.IsValid() || CoverageRenderTarget == nullptr)
	{
		return;
	}

	Capture.StampPendingTiles(*World, *NavMesh, *CoverageRenderTarget, Projection, Settings->MaxTilesStampedPerFrame);
}

ARecastNavMesh* UMinimapSubsystem::GetRecastNavMesh() const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (NavSys == nullptr)
	{
		return nullptr;
	}

	return Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance());
}

FBox2D UMinimapSubsystem::InflateBounds(const FBox2D& Bounds, float Fraction)
{
	const FVector2D Extent = Bounds.GetExtent();
	const double Margin = FMath::Max(Extent.X, Extent.Y) * Fraction;
	return FBox2D(
		Bounds.Min - FVector2D(Margin, Margin),
		Bounds.Max + FVector2D(Margin, Margin));
}
