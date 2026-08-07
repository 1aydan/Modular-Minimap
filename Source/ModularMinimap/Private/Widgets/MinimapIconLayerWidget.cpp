// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/MinimapIconLayerWidget.h"

#include "Components/MinimapTrackerComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/SlateBrush.h"
#include "Subsystems/MinimapSubsystem.h"

/** Convert a world yaw into a view-space direction on screen (x right, y down). */
static FVector2D WorldYawToScreenDir(float WorldYawDegrees, float ViewRotationRadians)
{
	const float Yaw = FMath::DegreesToRadians(WorldYawDegrees);

	// World forward (cos, sin) in map UV space: U follows world +Y, V follows world -X.
	const FVector2D UVDir(FMath::Sin(Yaw), -FMath::Cos(Yaw));

	// Map-to-screen applies R(-ViewRotation).
	const float Sin = FMath::Sin(-ViewRotationRadians);
	const float Cos = FMath::Cos(-ViewRotationRadians);
	return FVector2D(
		UVDir.X * Cos - UVDir.Y * Sin,
		UVDir.X * Sin + UVDir.Y * Cos);
}

UMinimapIconLayerWidget::UMinimapIconLayerWidget()
{
	PlayerIconStyle.Size = FVector2D(20.0, 20.0);
	PlayerIconStyle.FogRule = EMinimapFogRule::Always;
	PlayerIconStyle.EdgeClampMode = EMinimapEdgeClampMode::Clamp;
	PlayerIconStyle.ZOrder = 1000;
}

void UMinimapIconLayerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (PlayerIconStyle.Brush.GetResourceObject() == nullptr)
	{
		UTexture2D* ArrowTexture = UMinimapSubsystem::LoadDefaultIconTexture(TEXT("TX_PlayerArrow"));
		if (ArrowTexture)
		{
			PlayerIconStyle.Brush.SetResourceObject(ArrowTexture);
		}
	}

	if (EdgeArrowBrush.GetResourceObject() == nullptr)
	{
		UTexture2D* ArrowTexture = UMinimapSubsystem::LoadDefaultIconTexture(TEXT("TX_EdgeArrow"));
		if (ArrowTexture)
		{
			EdgeArrowBrush.SetResourceObject(ArrowTexture);
		}
	}
}

int32 UMinimapIconLayerWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	LastPaintHits.Reset();

	const UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem == nullptr)
	{
		return LayerId;
	}

	const FMinimapProjection Projection = Subsystem->GetProjection();
	if (!Projection.IsValid() || ViewState.UVSpan <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return LayerId;
	}

	const ESlateDrawEffect DrawEffects = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const double Side = FMath::Min(LocalSize.X, LocalSize.Y);
	const FVector2D Center = LocalSize * 0.5;

	// Unstyled map background fallback (used when the owning widget has no styling material).
	if (bDrawMapFallback)
	{
		UTexture* MapTexture = const_cast<UMinimapSubsystem*>(Subsystem)->GetBackgroundTexture();
		if (MapTexture)
		{
			const FVector2D BoxCenter = Center + (ViewState.MapUVToScreenNorm(FVector2D(0.5, 0.5)) - FVector2D(0.5, 0.5)) * Side;
			const double BoxSide = Side / ViewState.UVSpan;
			const FVector2D BoxSize(BoxSide, BoxSide);

			FSlateBrush MapBrush;
			MapBrush.SetResourceObject(MapTexture);
			MapBrush.ImageSize = BoxSize;

			FSlateDrawElement::MakeRotatedBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(BoxCenter - BoxSize * 0.5)),
				&MapBrush,
				DrawEffects,
				-ViewState.RotationRadians,
				TOptional<FVector2f>(),
				FSlateDrawElement::RelativeToElement,
				FallbackMapTint * InWidgetStyle.GetColorAndOpacityTint());
		}

		++LayerId;
	}

	// Gather icons.
	TArray<FPaintedIcon> Icons;

	for (const TWeakObjectPtr<UMinimapTrackerComponent>& WeakTracker : Subsystem->GetTrackedComponents())
	{
		const UMinimapTrackerComponent* Tracker = WeakTracker.Get();
		if (Tracker == nullptr || !Tracker->IsTrackerVisible())
		{
			continue;
		}

		const AActor* Owner = Tracker->GetOwner();
		if (Owner == nullptr)
		{
			continue;
		}

		const FMinimapIconStyle& Style = Tracker->IconStyle;
		if (!Subsystem->IsCategoryVisible(Style.CategoryTag))
		{
			continue;
		}

		if (Style.FogRule == EMinimapFogRule::RequireVisible && !Subsystem->IsWorldVisible(Owner->GetActorLocation()))
		{
			continue;
		}
		if (Style.FogRule == EMinimapFogRule::RequireExplored && !Subsystem->IsWorldExplored(Owner->GetActorLocation()))
		{
			continue;
		}

		FVector2D LocalPos = FVector2D::ZeroVector;
		bool bClamped = false;
		if (!ProjectAndClamp(Projection, LocalSize, Owner->GetActorLocation(), Style.EdgeClampMode, LocalPos, bClamped))
		{
			continue;
		}

		FPaintedIcon& Icon = Icons.AddDefaulted_GetRef();
		Icon.Size = Style.Size;
		Icon.TopLeft = LocalPos - Style.Size * 0.5;
		Icon.Tint = Style.GetRenderTint(InWidgetStyle);
		Icon.Brush = &Style.Brush;
		Icon.ZOrder = Style.ZOrder;
		Icon.Actor = const_cast<AActor*>(Owner);

		if (Style.bRotateWithActor && !bClamped)
		{
			Icon.AngleRadians = ComputeIconAngle(Owner->GetActorRotation().Yaw);
		}
	}

	// Objective markers.
	for (int32 ObjectiveIndex = 0; ObjectiveIndex < Subsystem->GetObjectives().Num(); ++ObjectiveIndex)
	{
		const FMinimapObjectiveEntry& Objective = Subsystem->GetObjectives()[ObjectiveIndex];
		if (!Objective.bActive || !Subsystem->IsCategoryVisible(Objective.Style.CategoryTag))
		{
			continue;
		}

		FVector WorldLocation = FVector::ZeroVector;
		if (!UMinimapSubsystem::ResolveObjectiveLocation(Objective, WorldLocation))
		{
			continue;
		}

		if (Objective.Style.FogRule == EMinimapFogRule::RequireVisible && !Subsystem->IsWorldVisible(WorldLocation))
		{
			continue;
		}
		if (Objective.Style.FogRule == EMinimapFogRule::RequireExplored && !Subsystem->IsWorldExplored(WorldLocation))
		{
			continue;
		}

		FVector2D LocalPos = FVector2D::ZeroVector;
		bool bClamped = false;
		if (!ProjectAndClamp(Projection, LocalSize, WorldLocation, Objective.Style.EdgeClampMode, LocalPos, bClamped))
		{
			continue;
		}

		FPaintedIcon& Icon = Icons.AddDefaulted_GetRef();
		Icon.Size = Objective.Style.Size;
		Icon.TopLeft = LocalPos - Objective.Style.Size * 0.5;
		Icon.Tint = Objective.Style.GetRenderTint(InWidgetStyle);
		Icon.Brush = &Objective.Style.Brush;
		Icon.ZOrder = Objective.Style.ZOrder;
		Icon.Objective.Index = ObjectiveIndex;
		Icon.Objective.Serial = Objective.Serial;

		if (bClamped && Objective.Style.EdgeClampMode == EMinimapEdgeClampMode::ClampWithArrow && EdgeArrowBrush.GetResourceObject() != nullptr)
		{
			const FVector2D Outward = LocalPos - Center;
			Icon.bDrawEdgeArrow = true;
			Icon.EdgeArrowAngleRadians = FMath::Atan2(Outward.Y, Outward.X) + UE_HALF_PI;
		}
	}

	Icons.StableSort([](const FPaintedIcon& A, const FPaintedIcon& B) { return A.ZOrder < B.ZOrder; });

	// Player marker paints on top.
	const APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController)
	{
		const APawn* Pawn = PlayerController->GetPawn();
		if (Pawn)
		{
			FVector2D LocalPos = FVector2D::ZeroVector;
			bool bClamped = false;
			if (ProjectAndClamp(Projection, LocalSize, Pawn->GetActorLocation(), PlayerIconStyle.EdgeClampMode, LocalPos, bClamped))
			{
				FPaintedIcon& Icon = Icons.AddDefaulted_GetRef();
				Icon.Size = PlayerIconStyle.Size;
				Icon.TopLeft = LocalPos - PlayerIconStyle.Size * 0.5;
				Icon.Tint = PlayerIconStyle.GetRenderTint(InWidgetStyle);
				Icon.Brush = &PlayerIconStyle.Brush;
				if (!bClamped)
				{
					Icon.AngleRadians = ComputeIconAngle(Pawn->GetActorRotation().Yaw);
				}
			}
		}
	}

	// Draw.
	for (const FPaintedIcon& Icon : Icons)
	{
		FSlateBrush IconBrush = *Icon.Brush;
		IconBrush.ImageSize = Icon.Size;

		FSlateDrawElement::MakeRotatedBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(Icon.Size, FSlateLayoutTransform(Icon.TopLeft)),
			&IconBrush,
			DrawEffects,
			Icon.AngleRadians,
			TOptional<FVector2f>(),
			FSlateDrawElement::RelativeToElement,
			Icon.Tint);

		if (Icon.bDrawEdgeArrow)
		{
			const FVector2D IconCenter = Icon.TopLeft + Icon.Size * 0.5;
			const FVector2D Outward = (IconCenter - Center).GetSafeNormal();
			const FVector2D ArrowCenter = IconCenter + Outward * (Icon.Size.X * 0.5 + EdgeArrowSize.X * 0.4);

			FSlateBrush ArrowBrush = EdgeArrowBrush;
			ArrowBrush.ImageSize = EdgeArrowSize;

			FSlateDrawElement::MakeRotatedBox(
				OutDrawElements,
				LayerId,
				AllottedGeometry.ToPaintGeometry(EdgeArrowSize, FSlateLayoutTransform(ArrowCenter - EdgeArrowSize * 0.5)),
				&ArrowBrush,
				DrawEffects,
				Icon.EdgeArrowAngleRadians,
				TOptional<FVector2f>(),
				FSlateDrawElement::RelativeToElement,
				EdgeArrowBrush.GetTint(InWidgetStyle) * Icon.Tint);
		}

		if (bClickableIcons && (Icon.Actor.IsValid() || Icon.Objective.IsValid()))
		{
			FIconHit& Hit = LastPaintHits.AddDefaulted_GetRef();
			Hit.Center = Icon.TopLeft + Icon.Size * 0.5;
			Hit.HalfSize = Icon.Size * 0.5;
			Hit.Actor = Icon.Actor;
			Hit.Objective = Icon.Objective;
		}
	}

	return LayerId + 1;
}

FReply UMinimapIconLayerWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bClickableIcons && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D LocalPos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());

		// Later entries drew on top; hit-test back to front.
		for (int32 Index = LastPaintHits.Num() - 1; Index >= 0; --Index)
		{
			const FIconHit& Hit = LastPaintHits[Index];
			if (FMath::Abs(LocalPos.X - Hit.Center.X) <= Hit.HalfSize.X &&
				FMath::Abs(LocalPos.Y - Hit.Center.Y) <= Hit.HalfSize.Y)
			{
				if (Hit.Objective.IsValid())
				{
					OnObjectiveClicked.Broadcast(Hit.Objective);
					UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
					if (Subsystem)
					{
						Subsystem->NotifyObjectiveClicked(Hit.Objective);
					}
					return FReply::Handled();
				}

				AActor* Actor = Hit.Actor.Get();
				if (Actor)
				{
					OnIconClicked.Broadcast(Actor);
					return FReply::Handled();
				}
			}
		}
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

bool UMinimapIconLayerWidget::ProjectAndClamp(const FMinimapProjection& Projection, const FVector2D& LocalSize, const FVector& WorldLocation,
	EMinimapEdgeClampMode ClampMode, FVector2D& OutLocalPos, bool& bOutClamped) const
{
	const double Side = FMath::Min(LocalSize.X, LocalSize.Y);
	const FVector2D Center = LocalSize * 0.5;

	const FVector2D ScreenNorm = ViewState.MapUVToScreenNorm(Projection.WorldToUV(WorldLocation));
	FVector2D Offset = (ScreenNorm - FVector2D(0.5, 0.5)) * Side;

	bOutClamped = false;

	if (ViewState.MaskShape == EMinimapMaskShape::Circle)
	{
		const double Radius = Side * 0.5 - EdgeClampPadding;
		const double Distance = Offset.Size();
		if (Distance > Radius)
		{
			if (ClampMode == EMinimapEdgeClampMode::Hide || Distance <= UE_DOUBLE_SMALL_NUMBER)
			{
				return false;
			}

			Offset *= Radius / Distance;
			bOutClamped = true;
		}
	}
	else
	{
		const FVector2D HalfExtent = LocalSize * 0.5 - FVector2D(EdgeClampPadding, EdgeClampPadding);
		const bool bInside = FMath::Abs(Offset.X) <= HalfExtent.X && FMath::Abs(Offset.Y) <= HalfExtent.Y;
		if (!bInside)
		{
			if (ClampMode == EMinimapEdgeClampMode::Hide)
			{
				return false;
			}

			const double ScaleX = FMath::Abs(Offset.X) > UE_DOUBLE_SMALL_NUMBER ? HalfExtent.X / FMath::Abs(Offset.X) : TNumericLimits<double>::Max();
			const double ScaleY = FMath::Abs(Offset.Y) > UE_DOUBLE_SMALL_NUMBER ? HalfExtent.Y / FMath::Abs(Offset.Y) : TNumericLimits<double>::Max();
			const double Scale = FMath::Min(ScaleX, ScaleY);
			if (Scale >= TNumericLimits<double>::Max())
			{
				return false;
			}

			Offset *= Scale;
			bOutClamped = true;
		}
	}

	OutLocalPos = Center + Offset;
	return true;
}

float UMinimapIconLayerWidget::ComputeIconAngle(float WorldYawDegrees) const
{
	const FVector2D ScreenDir = WorldYawToScreenDir(WorldYawDegrees, ViewState.RotationRadians);

	// Icon art points up; screen up is (0, -1), which is atan2 = -PI/2.
	return FMath::Atan2(ScreenDir.Y, ScreenDir.X) + UE_HALF_PI;
}
