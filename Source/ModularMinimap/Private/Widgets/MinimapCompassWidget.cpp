// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/MinimapCompassWidget.h"

#include "Components/MinimapTrackerComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Subsystems/MinimapSubsystem.h"

struct FCompassMark
{
	float BearingDelta = 0.0f;
	const FSlateBrush* Brush = nullptr;
	FLinearColor Tint = FLinearColor::White;
};

UMinimapCompassWidget::UMinimapCompassWidget()
{
	CardinalFont = FCoreStyle::GetDefaultFontStyle("Bold", 14);
}

int32 UMinimapCompassWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	const APlayerController* PlayerController = GetOwningPlayer();
	if (Subsystem == nullptr || PlayerController == nullptr)
	{
		return LayerId;
	}

	const ESlateDrawEffect DrawEffects = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const float ViewYaw = Subsystem->GetViewYawDegrees(PlayerController);

	const APawn* Pawn = PlayerController->GetPawn();
	const FVector ViewerLocation = Pawn != nullptr ? Pawn->GetActorLocation() : FVector::ZeroVector;

	auto BearingToX = [&](float BearingDelta) -> double
	{
		return (0.5 + BearingDelta / (2.0f * HalfFOVDegrees)) * LocalSize.X;
	};

	// Cardinal letters; north = world +X to match the map-up convention.
	static const TCHAR* CardinalLabels[] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
	static const float CardinalYaws[] = { 0.0f, 90.0f, 180.0f, 270.0f };

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Delta = FMath::FindDeltaAngleDegrees(ViewYaw, CardinalYaws[Index]);
		if (FMath::Abs(Delta) > HalfFOVDegrees)
		{
			continue;
		}

		const FVector2D TextPos(BearingToX(Delta) - 6.0, LocalSize.Y * 0.5 - 10.0);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(40.0, 24.0), FSlateLayoutTransform(TextPos)),
			FText::FromString(CardinalLabels[Index]),
			CardinalFont,
			DrawEffects,
			CardinalColor);
	}
	++LayerId;

	// Markers: tracked actors and objectives flagged for the compass.
	TArray<FCompassMark> Marks;

	for (const TWeakObjectPtr<UMinimapTrackerComponent>& WeakTracker : Subsystem->GetTrackedComponents())
	{
		const UMinimapTrackerComponent* Tracker = WeakTracker.Get();
		if (Tracker == nullptr || !Tracker->IsTrackerVisible() || !Tracker->IconStyle.bShowOnCompass)
		{
			continue;
		}

		const AActor* Owner = Tracker->GetOwner();
		if (Owner == nullptr || Owner == Pawn || !Subsystem->IsCategoryVisible(Tracker->IconStyle.CategoryTag))
		{
			continue;
		}

		const FVector Direction = Owner->GetActorLocation() - ViewerLocation;
		const float YawToTarget = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		const float Delta = FMath::FindDeltaAngleDegrees(ViewYaw, YawToTarget);
		if (FMath::Abs(Delta) > HalfFOVDegrees)
		{
			continue;
		}

		FCompassMark& Mark = Marks.AddDefaulted_GetRef();
		Mark.BearingDelta = Delta;
		Mark.Brush = &Tracker->IconStyle.Brush;
		Mark.Tint = Tracker->IconStyle.GetRenderTint(InWidgetStyle);
	}

	for (const FMinimapObjectiveEntry& Objective : Subsystem->GetObjectives())
	{
		if (!Objective.bActive || !Objective.Style.bShowOnCompass || !Subsystem->IsCategoryVisible(Objective.Style.CategoryTag))
		{
			continue;
		}

		FVector WorldLocation = FVector::ZeroVector;
		if (!UMinimapSubsystem::ResolveObjectiveLocation(Objective, WorldLocation))
		{
			continue;
		}

		const FVector Direction = WorldLocation - ViewerLocation;
		const float YawToTarget = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		const float Delta = FMath::FindDeltaAngleDegrees(ViewYaw, YawToTarget);
		if (FMath::Abs(Delta) > HalfFOVDegrees)
		{
			continue;
		}

		FCompassMark& Mark = Marks.AddDefaulted_GetRef();
		Mark.BearingDelta = Delta;
		Mark.Brush = &Objective.Style.Brush;
		Mark.Tint = Objective.Style.GetRenderTint(InWidgetStyle);
	}

	for (const FCompassMark& Mark : Marks)
	{
		FSlateBrush MarkBrush = *Mark.Brush;
		MarkBrush.ImageSize = CompassIconSize;

		const FVector2D MarkPos(BearingToX(Mark.BearingDelta) - CompassIconSize.X * 0.5, LocalSize.Y * 0.5 - CompassIconSize.Y * 0.5);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(CompassIconSize, FSlateLayoutTransform(MarkPos)),
			&MarkBrush,
			DrawEffects,
			Mark.Tint);
	}
	++LayerId;

	// Center (forward) marker.
	if (CenterMarkerBrush.GetResourceObject() != nullptr)
	{
		const FVector2D MarkerSize = CenterMarkerBrush.ImageSize;
		const FVector2D MarkerPos(LocalSize.X * 0.5 - MarkerSize.X * 0.5, 0.0);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(MarkerSize, FSlateLayoutTransform(MarkerPos)),
			&CenterMarkerBrush,
			DrawEffects,
			CenterMarkerBrush.GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint());
	}

	return LayerId + 1;
}
