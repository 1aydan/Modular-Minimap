// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/MinimapWidgetBase.h"
#include "MinimapWidget.generated.h"

/**
 * Corner minimap: follows the local player, optionally rotates with the view, and zooms through
 * a configurable list of world-span steps.
 */
UCLASS()
class MODULARMINIMAP_API UMinimapWidget : public UMinimapWidgetBase
{
	GENERATED_BODY()

public:
	UMinimapWidget();

	/** World-span zoom steps, small (zoomed in) to large (zoomed out). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	TArray<float> ZoomSteps;

	/** Index into ZoomSteps applied on construct. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	int32 InitialZoomStep = 1;

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void ZoomIn();

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void ZoomOut();

	UFUNCTION(BlueprintPure, Category = "Minimap")
	int32 GetZoomStep() const { return CurrentZoomStep; }

protected:
	virtual void NativeOnInitialized() override;

	void ApplyZoomStep(int32 Step);

	int32 CurrentZoomStep = 0;
};
