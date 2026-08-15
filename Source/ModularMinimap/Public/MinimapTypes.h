// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Styling/SlateBrush.h"
#include "Styling/WidgetStyle.h"
#include "MinimapTypes.generated.h"

/** How an icon behaves when its world position falls outside the visible map view. */
UENUM(BlueprintType)
enum class EMinimapEdgeClampMode : uint8
{
	/** The icon is hidden while outside the view. */
	Hide,
	/** The icon is pinned to the view edge. */
	Clamp,
	/** The icon is pinned to the view edge with a direction arrow. */
	ClampWithArrow
};

/** How fog of war gates an icon's visibility. */
UENUM(BlueprintType)
enum class EMinimapFogRule : uint8
{
	/** Visible regardless of fog state. */
	Always,
	/** Visible once its area has been explored. */
	RequireExplored,
	/** Visible only while inside current vision. */
	RequireVisible
};

/**
 * Tri-state fog-of-war switch. Used both by per-level settings and by the runtime override, so
 * "inherit" means "defer to the next level down": the runtime override falls back to the level
 * settings, which fall back to the project-wide bEnableFogOfWar.
 */
UENUM(BlueprintType)
enum class EMinimapFogMode : uint8
{
	/** No opinion; resolve from the level settings, then the project setting. */
	Inherit,
	/** Force fog of war on. */
	Enabled,
	/** Force fog of war off: the whole map reads as explored and visible. */
	Disabled
};

/** Mask shape applied to a minimap view. */
UENUM(BlueprintType)
enum class EMinimapMaskShape : uint8
{
	Circle,
	Rectangle
};

/**
 * Visual and behavioral configuration for one tracked icon, objective or compass marker.
 */
USTRUCT(BlueprintType)
struct MODULARMINIMAP_API FMinimapIconStyle
{
	GENERATED_BODY()

	/** Brush drawn for this icon. When unset, the plugin's default dot is used. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FSlateBrush Brush;

	/** Tint multiplied into the brush's own tint color. Leave white to draw the brush unmodified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FLinearColor Tint = FLinearColor::White;

	/** On-screen icon size in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FVector2D Size = FVector2D(12.0, 12.0);

	/** Category used for grouped show/hide, e.g. Minimap.Category.Enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	FGameplayTag CategoryTag;

	/** Behavior when the icon is outside the visible map view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	EMinimapEdgeClampMode EdgeClampMode = EMinimapEdgeClampMode::Hide;

	/** How fog of war gates this icon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	EMinimapFogRule FogRule = EMinimapFogRule::RequireVisible;

	/** When true, the icon rotates with the tracked actor's yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bRotateWithActor = false;

	/** When true, the icon also appears on the compass bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	bool bShowOnCompass = false;

	/** Draw order among icons; higher draws on top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Minimap")
	int32 ZOrder = 0;

	/**
	 * Color the icon should actually be drawn with.
	 *
	 * Slate's box elements tint solely from the color handed to FSlateDrawElement; a brush's own
	 * TintColor is never read by the draw path. Callers must therefore combine both here and pass
	 * the result as the draw element tint.
	 */
	FLinearColor GetRenderTint(const FWidgetStyle& InWidgetStyle) const
	{
		return Brush.GetTint(InWidgetStyle) * Tint * InWidgetStyle.GetColorAndOpacityTint();
	}
};

/**
 * Mapping between the world XY plane and map texture UV space.
 *
 * Convention: map "up" is world +X (north). The tight world bounds are letterboxed into a square
 * region (uniform scale on the larger XY extent) so map textures never stretch. U spans world +Y
 * (east to the right), V spans world -X (north up).
 */
USTRUCT(BlueprintType)
struct MODULARMINIMAP_API FMinimapProjection
{
	GENERATED_BODY()

	/** Tight world-space XY bounds the map covers. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	FBox2D WorldBounds = FBox2D(ForceInit);

	/** Center of the letterboxed square region mapped to the full UV range. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	FVector2D SquareCenter = FVector2D::ZeroVector;

	/** World units covered by the full UV range on both axes. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	double WorldSize = 0.0;

	bool IsValid() const
	{
		return WorldSize > UE_DOUBLE_KINDA_SMALL_NUMBER && WorldBounds.bIsValid;
	}

	void SetFromBounds(const FBox2D& InBounds)
	{
		WorldBounds = InBounds;
		SquareCenter = InBounds.GetCenter();
		const FVector2D Extent = InBounds.GetExtent();
		WorldSize = 2.0 * FMath::Max(Extent.X, Extent.Y);
	}

	FVector2D World2DToUV(const FVector2D& WorldXY) const
	{
		if (!IsValid())
		{
			return FVector2D(0.5, 0.5);
		}

		const double HalfSize = WorldSize * 0.5;
		return FVector2D(
			(WorldXY.Y - (SquareCenter.Y - HalfSize)) / WorldSize,
			1.0 - (WorldXY.X - (SquareCenter.X - HalfSize)) / WorldSize);
	}

	FVector2D WorldToUV(const FVector& WorldLocation) const
	{
		return World2DToUV(FVector2D(WorldLocation.X, WorldLocation.Y));
	}

	FVector2D UVToWorld2D(const FVector2D& UV) const
	{
		const double HalfSize = WorldSize * 0.5;
		return FVector2D(
			SquareCenter.X - HalfSize + (1.0 - UV.Y) * WorldSize,
			SquareCenter.Y - HalfSize + UV.X * WorldSize);
	}

	FVector2D World2DToPixel(const FVector2D& WorldXY, const FIntPoint& TextureSize) const
	{
		const FVector2D UV = World2DToUV(WorldXY);
		return FVector2D(UV.X * TextureSize.X, UV.Y * TextureSize.Y);
	}

	FVector2D WorldToPixel(const FVector& WorldLocation, const FIntPoint& TextureSize) const
	{
		return World2DToPixel(FVector2D(WorldLocation.X, WorldLocation.Y), TextureSize);
	}
};

/**
 * Opaque handle identifying an objective marker registered on the minimap subsystem.
 */
USTRUCT(BlueprintType)
struct MODULARMINIMAP_API FMinimapObjectiveHandle
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Index = INDEX_NONE;

	UPROPERTY()
	int32 Serial = 0;

	bool IsValid() const
	{
		return Index != INDEX_NONE;
	}

	bool operator==(const FMinimapObjectiveHandle& Other) const
	{
		return Index == Other.Index && Serial == Other.Serial;
	}

	friend uint32 GetTypeHash(const FMinimapObjectiveHandle& Handle)
	{
		return HashCombine(::GetTypeHash(Handle.Index), ::GetTypeHash(Handle.Serial));
	}
};

/**
 * Snapshot of one map view (minimap or full map) for a frame: which map UV is centered, how much
 * of the map is visible, and how the view is rotated. Produced by the widget base, consumed by the
 * icon layer and the styling material.
 */
USTRUCT(BlueprintType)
struct MODULARMINIMAP_API FMinimapViewState
{
	GENERATED_BODY()

	/** Map UV at the widget center. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	FVector2D CenterUV = FVector2D(0.5, 0.5);

	/** UV units spanned across the widget's smaller dimension (zoom). */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	double UVSpan = 1.0;

	/** View rotation in radians. Screen-to-map: MapUV = CenterUV + R(Rotation) * (ScreenNorm - 0.5) * UVSpan. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	float RotationRadians = 0.0f;

	/** Mask shape used for icon containment and edge clamping. */
	UPROPERTY(BlueprintReadOnly, Category = "Minimap")
	EMinimapMaskShape MaskShape = EMinimapMaskShape::Circle;

	/** Transform a map UV to normalized screen space (0..1 across the widget's centered square). */
	FVector2D MapUVToScreenNorm(const FVector2D& MapUV) const
	{
		const FVector2D Delta = (MapUV - CenterUV) / UVSpan;
		const float Sin = FMath::Sin(-RotationRadians);
		const float Cos = FMath::Cos(-RotationRadians);
		return FVector2D(
			0.5 + Delta.X * Cos - Delta.Y * Sin,
			0.5 + Delta.X * Sin + Delta.Y * Cos);
	}

	/** Transform a normalized screen position back to a map UV. */
	FVector2D ScreenNormToMapUV(const FVector2D& ScreenNorm) const
	{
		const FVector2D Local = ScreenNorm - FVector2D(0.5, 0.5);
		const float Sin = FMath::Sin(RotationRadians);
		const float Cos = FMath::Cos(RotationRadians);
		const FVector2D Rotated(
			Local.X * Cos - Local.Y * Sin,
			Local.X * Sin + Local.Y * Cos);
		return CenterUV + Rotated * UVSpan;
	}
};

/** Broadcast after the map projection changed (initial resolve or bounds growth). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMinimapBoundsChanged);

/** Broadcast when the background texture object widgets should display changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMinimapMapTextureChanged);

/** Broadcast when an objective marker is clicked on a map view. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapObjectiveClicked, FMinimapObjectiveHandle, Handle);

/** Broadcast when fog of war is switched on or off for the world. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMinimapFogEnabledChanged, bool, bEnabled);
