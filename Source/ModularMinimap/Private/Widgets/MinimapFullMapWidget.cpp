// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/MinimapFullMapWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Widgets/MinimapFullMapViewWidget.h"

UMinimapFullMapWidget::UMinimapFullMapWidget()
{
	bIsBackHandler = true;
}

void UMinimapFullMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("AutoRoot"));
		WidgetTree->RootWidget = Root;

		MapView = WidgetTree->ConstructWidget<UMinimapFullMapViewWidget>(UMinimapFullMapViewWidget::StaticClass(), TEXT("AutoMapView"));
		UOverlaySlot* ViewSlot = Root->AddChildToOverlay(MapView);
		if (ViewSlot)
		{
			ViewSlot->SetHorizontalAlignment(HAlign_Fill);
			ViewSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
}

void UMinimapFullMapWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	RecenterView();
}

bool UMinimapFullMapWidget::NativeOnHandleBackAction()
{
	DeactivateWidget();
	return true;
}

TOptional<FUIInputConfig> UMinimapFullMapWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(InputMode, EMouseCaptureMode::NoCapture);
}

void UMinimapFullMapWidget::PanView(FVector2D ScreenDelta)
{
	if (MapView != nullptr)
	{
		MapView->PanByScreenDelta(ScreenDelta);
	}
}

void UMinimapFullMapWidget::ZoomView(float Multiplier)
{
	if (MapView != nullptr)
	{
		MapView->ZoomByClamped(Multiplier);
	}
}

void UMinimapFullMapWidget::RecenterView()
{
	if (MapView != nullptr)
	{
		MapView->Recenter();
	}
}
