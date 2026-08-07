// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/MinimapBoundsVolume.h"

#include "Components/BrushComponent.h"

AMinimapBoundsVolume::AMinimapBoundsVolume()
{
	UBrushComponent* VolumeBrush = GetBrushComponent();
	if (VolumeBrush)
	{
		VolumeBrush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		VolumeBrush->SetGenerateOverlapEvents(false);
	}

	SetCanBeDamaged(false);
	SetActorEnableCollision(false);
}

FBox2D AMinimapBoundsVolume::GetWorldBounds2D() const
{
	const FBox Bounds = GetComponentsBoundingBox(true);
	return FBox2D(
		FVector2D(Bounds.Min.X, Bounds.Min.Y),
		FVector2D(Bounds.Max.X, Bounds.Max.Y));
}
