// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/MinimapWidget.h"

UMinimapWidget::UMinimapWidget()
{
	ZoomSteps = { 3000.0f, 6000.0f, 12000.0f };
}

void UMinimapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	ApplyZoomStep(InitialZoomStep);
}

void UMinimapWidget::ZoomIn()
{
	ApplyZoomStep(CurrentZoomStep - 1);
}

void UMinimapWidget::ZoomOut()
{
	ApplyZoomStep(CurrentZoomStep + 1);
}

void UMinimapWidget::ApplyZoomStep(int32 Step)
{
	if (ZoomSteps.IsEmpty())
	{
		return;
	}

	CurrentZoomStep = FMath::Clamp(Step, 0, ZoomSteps.Num() - 1);
	SetViewWorldSpan(ZoomSteps[CurrentZoomStep]);
}
