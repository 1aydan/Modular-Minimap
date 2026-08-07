// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"
#include "CommonInputModeTypes.h"
#include "CoreMinimal.h"
#include "MinimapFullMapWidget.generated.h"

class UMinimapFullMapViewWidget;

/**
 * Full-screen map overlay screen (PoE style). A CommonUI activatable widget the game pushes onto
 * its own layer stack; the CommonUI back action (Esc / gamepad B) closes it. Contains a pannable
 * north-up map view, auto-created when no widget-blueprint tree is provided.
 *
 * Gamepad pan/zoom: bind your input actions and call PanView / ZoomView / RecenterView.
 */
UCLASS()
class MODULARMINIMAP_API UMinimapFullMapWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UMinimapFullMapWidget();

	/** Input mode requested while the map is open. All = game keeps receiving input (PoE-style overlay). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Minimap")
	ECommonInputMode InputMode = ECommonInputMode::All;

	/** The pannable map view. Optional in widget blueprints; auto-created when the tree is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap", meta = (BindWidgetOptional))
	TObjectPtr<UMinimapFullMapViewWidget> MapView;

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void PanView(FVector2D ScreenDelta);

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void ZoomView(float Multiplier);

	UFUNCTION(BlueprintCallable, Category = "Minimap")
	void RecenterView();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
