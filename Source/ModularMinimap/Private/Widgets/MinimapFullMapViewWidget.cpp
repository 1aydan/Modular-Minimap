// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/MinimapFullMapViewWidget.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/MinimapSubsystem.h"
#include "Widgets/MinimapIconLayerWidget.h"

UMinimapFullMapViewWidget::UMinimapFullMapViewWidget()
{
	MaskShape = EMinimapMaskShape::Rectangle;
	bRotateWithView = false;
	ViewWorldSpan = 20000.0f;
	bClampViewToBounds = true;
}

void UMinimapFullMapViewWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IconLayer != nullptr)
	{
		IconLayer->bClickableIcons = true;
	}

	SetVisibility(ESlateVisibility::Visible);
	Recenter();
}

void UMinimapFullMapViewWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Keep the pan scale valid for gamepad pans that happen without a preceding mouse press.
	const FVector2D LocalSize = MyGeometry.GetLocalSize();
	const double Side = FMath::Min(LocalSize.X, LocalSize.Y);
	if (Side > 1.0)
	{
		CachedSideLength = Side;
	}
}

FVector UMinimapFullMapViewWidget::GetViewCenterWorld() const
{
	return FVector(PanCenterWorld.X, PanCenterWorld.Y, 0.0);
}

void UMinimapFullMapViewWidget::PanByScreenDelta(FVector2D ScreenDelta)
{
	const UMinimapSubsystem* Subsystem = UMinimapSubsystem::Get(this);
	if (Subsystem == nullptr)
	{
		return;
	}

	const FMinimapProjection Projection = Subsystem->GetProjection();
	if (!Projection.IsValid() || CachedSideLength <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Screen delta -> map UV delta (view is north-up, so no rotation term by default).
	const float Sin = FMath::Sin(ViewState.RotationRadians);
	const float Cos = FMath::Cos(ViewState.RotationRadians);
	const FVector2D NormDelta = ScreenDelta / CachedSideLength * ViewState.UVSpan;
	const FVector2D UVDelta(
		NormDelta.X * Cos - NormDelta.Y * Sin,
		NormDelta.X * Sin + NormDelta.Y * Cos);

	// UV u follows world +Y; UV v runs opposite world X (map-up = +X).
	PanCenterWorld.Y += UVDelta.X * Projection.WorldSize;
	PanCenterWorld.X -= UVDelta.Y * Projection.WorldSize;

	// Keep the center inside the mapped square.
	const double HalfSize = Projection.WorldSize * 0.5;
	PanCenterWorld.X = FMath::Clamp(PanCenterWorld.X, Projection.SquareCenter.X - HalfSize, Projection.SquareCenter.X + HalfSize);
	PanCenterWorld.Y = FMath::Clamp(PanCenterWorld.Y, Projection.SquareCenter.Y - HalfSize, Projection.SquareCenter.Y + HalfSize);
}

void UMinimapFullMapViewWidget::Recenter()
{
	const APlayerController* PlayerController = GetOwningPlayer();
	if (PlayerController)
	{
		const APawn* Pawn = PlayerController->GetPawn();
		if (Pawn)
		{
			const FVector Location = Pawn->GetActorLocation();
			PanCenterWorld = FVector2D(Location.X, Location.Y);
		}
	}

	SetViewWorldSpan(FMath::Clamp(ViewWorldSpan, MinViewWorldSpan, MaxViewWorldSpan));
}

void UMinimapFullMapViewWidget::ZoomByClamped(float Multiplier)
{
	if (Multiplier > UE_KINDA_SMALL_NUMBER)
	{
		SetViewWorldSpan(FMath::Clamp(ViewWorldSpan * Multiplier, MinViewWorldSpan, MaxViewWorldSpan));
	}
}

FReply UMinimapFullMapViewWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = true;
		LastDragPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		const FVector2D LocalSize = InGeometry.GetLocalSize();
		CachedSideLength = FMath::Max(1.0, FMath::Min(LocalSize.X, LocalSize.Y));
		return FReply::Handled().CaptureMouse(GetCachedWidget().ToSharedRef());
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UMinimapFullMapViewWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging)
	{
		const FVector2D LocalPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		PanByScreenDelta(-(LocalPosition - LastDragPosition));
		LastDragPosition = LocalPosition;
		return FReply::Handled();
	}

	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UMinimapFullMapViewWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bDragging && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}

	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UMinimapFullMapViewWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const float Multiplier = InMouseEvent.GetWheelDelta() > 0.0f ? 1.0f / WheelZoomFactor : WheelZoomFactor;
	ZoomByClamped(Multiplier);
	return FReply::Handled();
}

void UMinimapFullMapViewWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	bDragging = false;
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}
