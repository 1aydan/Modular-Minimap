// Copyright Epic Games, Inc. All Rights Reserved.

#include "MinimapAssetFactory.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Data/MinimapLevelSettings.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialFactoryNew.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialShared.h"
#include "Misc/PackageName.h"
#include "ShaderCompiler.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogModularMinimapAssets, Log, All);

static constexpr int32 IconTextureSize = 64;

static bool SaveNewAsset(UObject* Asset)
{
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);

	const FString FileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	return UPackage::SavePackage(Package, Asset, *FileName, SaveArgs);
}

static float EdgeFunction(const FVector2D& A, const FVector2D& B, const FVector2D& P)
{
	return (P.X - A.X) * (B.Y - A.Y) - (P.Y - A.Y) * (B.X - A.X);
}

static bool PointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	const float E0 = EdgeFunction(A, B, P);
	const float E1 = EdgeFunction(B, C, P);
	const float E2 = EdgeFunction(C, A, P);
	return (E0 >= 0.0f && E1 >= 0.0f && E2 >= 0.0f) || (E0 <= 0.0f && E1 <= 0.0f && E2 <= 0.0f);
}

/** Rasterize a unit-space shape predicate into a BGRA icon with 2x2 supersampling. */
static UTexture2D* CreateIconTexture(const FString& Name, TFunctionRef<bool(const FVector2D&)> InsideShape)
{
	const FString PackagePath = FString(TEXT("/ModularMinimap/Textures")) / Name;
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		UE_LOG(LogModularMinimapAssets, Log, TEXT("%s already exists; skipping."), *PackagePath);
		return LoadObject<UTexture2D>(nullptr, *(PackagePath + TEXT(".") + Name));
	}

	TArray<uint8> Pixels;
	Pixels.SetNumZeroed(IconTextureSize * IconTextureSize * 4);

	for (int32 Y = 0; Y < IconTextureSize; ++Y)
	{
		for (int32 X = 0; X < IconTextureSize; ++X)
		{
			int32 Hits = 0;
			for (int32 SubY = 0; SubY < 2; ++SubY)
			{
				for (int32 SubX = 0; SubX < 2; ++SubX)
				{
					const FVector2D Point(
						(X + 0.25 + SubX * 0.5) / IconTextureSize,
						(Y + 0.25 + SubY * 0.5) / IconTextureSize);
					if (InsideShape(Point))
					{
						++Hits;
					}
				}
			}

			const uint8 Alpha = static_cast<uint8>(Hits * 255 / 4);
			uint8* Pixel = &Pixels[(Y * IconTextureSize + X) * 4];
			Pixel[0] = 255; // B
			Pixel[1] = 255; // G
			Pixel[2] = 255; // R
			Pixel[3] = Alpha;
		}
	}

	UPackage* Package = CreatePackage(*PackagePath);
	UTexture2D* Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
	Texture->Source.Init(IconTextureSize, IconTextureSize, 1, 1, TSF_BGRA8, Pixels.GetData());
	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;
	Texture->LODGroup = TEXTUREGROUP_UI;
	Texture->UpdateResource();
	Texture->PostEditChange();
	SaveNewAsset(Texture);

	UE_LOG(LogModularMinimapAssets, Log, TEXT("Created %s"), *PackagePath);
	return Texture;
}

/**
 * Create the styling material asset, or clear an existing one for an in-place rebuild.
 * Returns null when the asset already exists and no rebuild was asked for, or on failure.
 */
static UMaterial* PrepareMaterialAsset(const FString& PackagePath, const TCHAR* AssetName, bool bForce)
{
	UMaterial* Material = nullptr;
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		if (!bForce)
		{
			UE_LOG(LogModularMinimapAssets, Log, TEXT("%s already exists; skipping (pass 'force' to rebuild)."), *PackagePath);
			return nullptr;
		}

		// Rebuild the existing asset in place so referencing content keeps working.
		Material = LoadObject<UMaterial>(nullptr, *(PackagePath + TEXT(".") + AssetName));
		if (Material != nullptr)
		{
			// An open material editor owns its own UMaterialGraph and re-syncs it onto the asset,
			// so rebuild against a closed asset.
			if (GEditor != nullptr)
			{
				if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					AssetEditorSubsystem->CloseAllEditorsForAsset(Material);
				}
			}

			// Neither UMaterialEditingLibrary::DeleteAllMaterialExpressions (it range-iterates the
			// expression array while DeleteMaterialExpression removes from that same array, so it
			// skips every other entry and leaves orphans behind) nor a manual DeleteMaterialExpression
			// loop (it ends in MarkAsGarbage, which asserts on any expression the engine has rooted
			// and takes the whole editor down mid-rebuild). The entire graph is being replaced, so
			// drop the collection wholesale and let GC reclaim it.
			const int32 NumCleared = Material->GetExpressions().Num();
			Material->GetExpressionCollection().Empty();

			// Property inputs still point at the detached expressions; the rebuild reconnects the
			// ones it uses, but every slot has to be cleared or stale nodes survive the rebuild.
			for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
			{
				if (FExpressionInput* Input = Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(PropertyIndex)))
				{
					Input->Expression = nullptr;
				}
			}

			UE_LOG(LogModularMinimapAssets, Log, TEXT("Rebuilding %s (cleared %d expressions)."), *PackagePath, NumCleared);
		}
	}

	if (Material == nullptr)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, AssetName, RF_Public | RF_Standalone, nullptr, GWarn));
	}

	if (Material == nullptr)
	{
		UE_LOG(LogModularMinimapAssets, Warning, TEXT("Failed to create %s."), *PackagePath);
		return nullptr;
	}

	Material->MaterialDomain = MD_UI;
	Material->BlendMode = BLEND_Translucent;
	return Material;
}

static UMaterialExpression* MakeTextureParam(UMaterial* Material, const FName Name, int32 PosY)
{
	UMaterialExpressionTextureObjectParameter* Param = Cast<UMaterialExpressionTextureObjectParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureObjectParameter::StaticClass(), -1100, PosY));
	Param->ParameterName = Name;
	Param->Texture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	Param->SamplerType = SAMPLERTYPE_LinearColor;
	return Param;
}

static UMaterialExpression* MakeScalarParam(UMaterial* Material, const FName Name, float Default, int32 PosY)
{
	UMaterialExpressionScalarParameter* Param = Cast<UMaterialExpressionScalarParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -1100, PosY));
	Param->ParameterName = Name;
	Param->DefaultValue = Default;
	return Param;
}

static UMaterialExpression* MakeVectorParam(UMaterial* Material, const FName Name, const FLinearColor& Default, int32 PosY)
{
	UMaterialExpressionVectorParameter* Param = Cast<UMaterialExpressionVectorParameter>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -1100, PosY));
	Param->ParameterName = Name;
	Param->DefaultValue = Default;
	return Param;
}

/**
 * Build the compositing Custom node and wire the named sources into it.
 *
 * SourceOutputs selects which output pin of each source to take: a vector parameter's default
 * output is RGB (float3), so any parameter whose alpha the HLSL reads must be listed as "RGBA".
 */
static UMaterialExpressionCustom* BuildCompositeNode(
	UMaterial* Material,
	const TCHAR* Code,
	TConstArrayView<const TCHAR*> InputNames,
	TConstArrayView<UMaterialExpression*> Sources,
	TConstArrayView<const TCHAR*> SourceOutputs)
{
	check(InputNames.Num() == Sources.Num() && InputNames.Num() == SourceOutputs.Num());

	UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), -500, 0));
	Custom->Code = Code;
	Custom->OutputType = CMOT_Float4;
	Custom->Description = TEXT("MinimapComposite");
	Custom->Inputs.Empty();

	for (const TCHAR* InputName : InputNames)
	{
		FCustomInput Input;
		Input.InputName = InputName;
		Custom->Inputs.Add(Input);
	}

	for (int32 Index = 0; Index < InputNames.Num(); ++Index)
	{
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Sources[Index], SourceOutputs[Index], Custom, InputNames[Index]))
		{
			UE_LOG(LogModularMinimapAssets, Warning, TEXT("Failed to connect custom node input '%s'."), InputNames[Index]);
		}
	}

	return Custom;
}

/** Split the composite node's float4 into the emissive and opacity pins, then compile and save. */
static void FinalizeCompositeMaterial(UMaterial* Material, UMaterialExpressionCustom* Custom, const FString& PackagePath)
{
	UMaterialExpressionComponentMask* ColorMask = Cast<UMaterialExpressionComponentMask>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -200, -50));
	ColorMask->R = 1;
	ColorMask->G = 1;
	ColorMask->B = 1;
	ColorMask->A = 0;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Custom, FString(), ColorMask, FString());

	UMaterialExpressionComponentMask* AlphaMask = Cast<UMaterialExpressionComponentMask>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), -200, 100));
	AlphaMask->R = 0;
	AlphaMask->G = 0;
	AlphaMask->B = 0;
	AlphaMask->A = 1;
	UMaterialEditingLibrary::ConnectMaterialExpressions(Custom, FString(), AlphaMask, FString());

	UMaterialEditingLibrary::ConnectMaterialProperty(ColorMask, FString(), MP_EmissiveColor);
	UMaterialEditingLibrary::ConnectMaterialProperty(AlphaMask, FString(), MP_Opacity);

	UMaterialEditingLibrary::RecompileMaterial(Material);
	SaveNewAsset(Material);

	UE_LOG(LogModularMinimapAssets, Log, TEXT("Created %s"), *PackagePath);
}

/**
 * Composites the coverage mask, fog and view mask into the styled map the widgets display.
 *
 * The part worth understanding is how the walkable edge is found. The obvious approach -- dilate the
 * mask by a texel offset and subtract -- yields a hard-edged ribbon whose width follows the map texel
 * grid: it thickens as the view zooms in, thins on diagonals, and stair-steps because the coverage
 * mask is a binary rasterisation. Instead a small weighted ring gather blurs the mask into a ramp,
 * that ramp is converted back into a signed distance to the boundary, and every visual band is a
 * smoothstep on that distance expressed in *screen pixels*. Outline width, softness, offset, fill
 * feather and glow are therefore all independent of zoom, map resolution and widget size.
 */
static const TCHAR* MinimapMaterialHLSL = TEXT(R"HLSL(
// Base is widget-normalized (edges at +-0.5 on both axes) and drives the mask; Local is corrected
// so one unit means the same number of pixels on both axes, matching how the icon layer projects.
float2 Base = UV - 0.5f;
float2 Local = Base * AspectScale.rg;
float S = sin(RotationRad);
float C = cos(RotationRad);
float2 MapUV = CenterUV.rg + float2(Local.x * C - Local.y * S, Local.x * S + Local.y * C) * UVSpan;

// Screen derivatives give the map-UV footprint of one output pixel. Taken here, outside any
// branching, because derivatives inside flow control are not well defined.
float PixelUV = max(max(length(ddx(MapUV)), length(ddy(MapUV))), 1e-7f);

// Fade over a slice of the view rather than cutting hard, so a level edge crossing the view reads
// as the map ending rather than as the widget failing to draw.
float2 EdgeDistance = min(MapUV, 1.0f - MapUV);
float InMap = saturate(min(EdgeDistance.x, EdgeDistance.y) / max(UVSpan * 0.02f, 1e-5f));
float2 SampleUV = clamp(MapUV, 0.0f, 1.0f);

float4 MapSample = Texture2DSample(MapTex, MapTexSampler, SampleUV);

float3 BaseColor;
float BaseAlpha;
if (BackgroundIsMask > 0.5f)
{
// The kernel has to span everything the shading below asks about, and can never resolve finer than
// the coverage texture itself. Clamping to one texel is what makes the outline fade out gracefully
// when zoomed far enough that a screen pixel covers several texels, instead of shimmering.
float TexelUV = 1.0f / max(MapTexelCount, 1.0f);
float ReachPixels = max(OutlineWidthPixels * 0.5f + OutlineSoftnessPixels + abs(OutlineOffsetPixels) + GlowWidthPixels, 1.0f);
float Radius = max(ReachPixels * PixelUV, TexelUV);

// Two rings, the outer offset by half a step to keep the pattern from favouring the axes, plus the
// centre tap. Blurring the hard 0/1 coverage mask produces a ramp that is close to linear in
// distance across the kernel, and it is that ramp the shading reads instead of the mask.
float Sum = MapSample.r;
float WeightSum = 1.0f;
[unroll]
for (int TapIndex = 0; TapIndex < 8; ++TapIndex)
{
float Angle = float(TapIndex) * 0.78539816f;
float2 Inner = float2(cos(Angle), sin(Angle));
float2 Outer = float2(cos(Angle + 0.39269908f), sin(Angle + 0.39269908f));
Sum += Texture2DSampleLevel(MapTex, MapTexSampler, clamp(SampleUV + Inner * Radius * 0.55f, 0.0f, 1.0f), 0.0f).r * 0.75f;
Sum += Texture2DSampleLevel(MapTex, MapTexSampler, clamp(SampleUV + Outer * Radius, 0.0f, 1.0f), 0.0f).r * 0.40f;
WeightSum += 1.15f;
}
float Coverage = Sum / WeightSum;

// Invert the ramp back into a signed distance to the boundary, positive inside the walkable area.
// Accurate near the edge, which is the only place anything is drawn.
float DistancePixels = (Coverage - 0.5f) * 2.0f * Radius / PixelUV;

float HalfWidth = OutlineWidthPixels * 0.5f;
float Softness = max(OutlineSoftnessPixels, 0.01f);
float BandDistance = abs(DistancePixels - OutlineOffsetPixels);
float Outline = 1.0f - smoothstep(HalfWidth - Softness * 0.5f, HalfWidth + Softness * 0.5f, BandDistance);

float Fill = saturate(0.5f + DistancePixels / max(FillFeatherPixels, 0.01f));

// The glow only reaches outward, and never over the outline it comes from.
float GlowDistance = max(-DistancePixels - HalfWidth, 0.0f);
float Glow = GlowWidthPixels > 0.001f ? (1.0f - smoothstep(0.0f, GlowWidthPixels, GlowDistance)) * (1.0f - Outline) : 0.0f;

// Composite back to front in premultiplied alpha, then undo the premultiply: the material drives
// colour and opacity on separate pins.
float GlowAlpha = GlowColor.a * Glow;
float3 Premultiplied = GlowColor.rgb * GlowAlpha;
float Alpha = GlowAlpha;

float FillAlpha = FillColor.a * Fill;
Premultiplied = Premultiplied * (1.0f - FillAlpha) + FillColor.rgb * FillAlpha;
Alpha = Alpha * (1.0f - FillAlpha) + FillAlpha;

float OutlineAlpha = OutlineColor.a * Outline;
Premultiplied = Premultiplied * (1.0f - OutlineAlpha) + OutlineColor.rgb * OutlineAlpha;
Alpha = Alpha * (1.0f - OutlineAlpha) + OutlineAlpha;

BaseColor = Alpha > 1e-5f ? Premultiplied / Alpha : float3(0.0f, 0.0f, 0.0f);
BaseAlpha = Alpha;
}
else
{
BaseColor = MapSample.rgb;
BaseAlpha = MapSample.a;
}

float Vis = 1.0f;
if (FogEnabled > 0.5f)
{
float Explored = Texture2DSample(FogE, FogESampler, SampleUV).r;
float Visible = Texture2DSample(FogV, FogVSampler, SampleUV).r;
Vis = max(saturate(Explored) * ExploredDim, saturate(Visible));
}

// Circle inscribes to the smaller dimension (uniform Local); rectangle fills the widget (Base).
float MaskDist = MaskShape > 0.5f ? length(Local) * 2.0f : max(abs(Base.x), abs(Base.y)) * 2.0f;
float Mask = 1.0f - smoothstep(0.94f, 1.0f, MaskDist);

// Off-map area keeps the view shape intact instead of punching a hole through it.
float3 FinalColor = lerp(VoidColor.rgb, BaseColor * Vis, InMap);
float FinalAlpha = lerp(VoidColor.a, BaseAlpha * Vis, InMap) * Mask;

return float4(FinalColor, FinalAlpha);
)HLSL");

// A vector parameter's default output is RGB (float3); the HLSL reads .a on some of them, so those
// must connect through the explicit RGBA output.
static const TCHAR* MinimapParamOutputRGBA = TEXT("RGBA");
static const TCHAR* MinimapParamOutputDefault = TEXT("");

static void CreateMinimapMaterial(bool bForce)
{
	const FString PackagePath = TEXT("/ModularMinimap/Materials/M_MinimapBase");
	UMaterial* Material = PrepareMaterialAsset(PackagePath, TEXT("M_MinimapBase"), bForce);
	if (Material == nullptr)
	{
		return;
	}

	const UMinimapLevelSettings* StyleDefaults = GetDefault<UMinimapLevelSettings>();

	UMaterialExpression* TexCoord = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1100, -500);
	UMaterialExpression* MapTex = MakeTextureParam(Material, TEXT("MapTexture"), -400);
	UMaterialExpression* FogE = MakeTextureParam(Material, TEXT("FogExplored"), -250);
	UMaterialExpression* FogV = MakeTextureParam(Material, TEXT("FogVisible"), -100);
	UMaterialExpression* CenterUV = MakeVectorParam(Material, TEXT("CenterUV"), FLinearColor(0.5f, 0.5f, 0.0f, 0.0f), 50);
	UMaterialExpression* AspectScale = MakeVectorParam(Material, TEXT("AspectScale"), FLinearColor(1.0f, 1.0f, 0.0f, 0.0f), 200);
	UMaterialExpression* FillColor = MakeVectorParam(Material, TEXT("FillColor"), StyleDefaults->WalkableFillColor, 350);
	UMaterialExpression* OutlineColor = MakeVectorParam(Material, TEXT("OutlineColor"), StyleDefaults->OutlineColor, 500);
	UMaterialExpression* GlowColor = MakeVectorParam(Material, TEXT("GlowColor"), StyleDefaults->GlowColor, 650);
	UMaterialExpression* VoidColor = MakeVectorParam(Material, TEXT("VoidColor"), FLinearColor(0.02f, 0.02f, 0.03f, 0.35f), 800);
	UMaterialExpression* UVSpan = MakeScalarParam(Material, TEXT("UVSpan"), 1.0f, 950);
	UMaterialExpression* RotationRad = MakeScalarParam(Material, TEXT("RotationRad"), 0.0f, 1020);
	UMaterialExpression* MaskShape = MakeScalarParam(Material, TEXT("MaskShape"), 1.0f, 1090);
	UMaterialExpression* MapTexelCount = MakeScalarParam(Material, TEXT("MapTexelCount"), 1024.0f, 1160);
	UMaterialExpression* OutlineWidthPixels = MakeScalarParam(Material, TEXT("OutlineWidthPixels"), StyleDefaults->OutlineWidthPixels, 1230);
	UMaterialExpression* OutlineSoftnessPixels = MakeScalarParam(Material, TEXT("OutlineSoftnessPixels"), StyleDefaults->OutlineSoftnessPixels, 1300);
	UMaterialExpression* OutlineOffsetPixels = MakeScalarParam(Material, TEXT("OutlineOffsetPixels"), StyleDefaults->OutlineOffsetPixels, 1370);
	UMaterialExpression* FillFeatherPixels = MakeScalarParam(Material, TEXT("FillFeatherPixels"), StyleDefaults->FillFeatherPixels, 1440);
	UMaterialExpression* GlowWidthPixels = MakeScalarParam(Material, TEXT("GlowWidthPixels"), StyleDefaults->GlowWidthPixels, 1510);
	UMaterialExpression* ExploredDim = MakeScalarParam(Material, TEXT("ExploredDim"), 0.45f, 1580);
	UMaterialExpression* FogEnabled = MakeScalarParam(Material, TEXT("FogEnabled"), 0.0f, 1650);
	UMaterialExpression* BackgroundIsMask = MakeScalarParam(Material, TEXT("BackgroundIsMask"), 1.0f, 1720);

	const TCHAR* InputNames[] = {
		TEXT("UV"), TEXT("MapTex"), TEXT("FogE"), TEXT("FogV"), TEXT("CenterUV"), TEXT("AspectScale"),
		TEXT("FillColor"), TEXT("OutlineColor"), TEXT("GlowColor"), TEXT("VoidColor"),
		TEXT("UVSpan"), TEXT("RotationRad"), TEXT("MaskShape"), TEXT("MapTexelCount"),
		TEXT("OutlineWidthPixels"), TEXT("OutlineSoftnessPixels"), TEXT("OutlineOffsetPixels"),
		TEXT("FillFeatherPixels"), TEXT("GlowWidthPixels"),
		TEXT("ExploredDim"), TEXT("FogEnabled"), TEXT("BackgroundIsMask") };

	UMaterialExpression* Sources[] = {
		TexCoord, MapTex, FogE, FogV, CenterUV, AspectScale,
		FillColor, OutlineColor, GlowColor, VoidColor,
		UVSpan, RotationRad, MaskShape, MapTexelCount,
		OutlineWidthPixels, OutlineSoftnessPixels, OutlineOffsetPixels,
		FillFeatherPixels, GlowWidthPixels,
		ExploredDim, FogEnabled, BackgroundIsMask };

	const TCHAR* SourceOutputs[] = {
		MinimapParamOutputDefault, MinimapParamOutputDefault, MinimapParamOutputDefault, MinimapParamOutputDefault, MinimapParamOutputRGBA, MinimapParamOutputRGBA,
		MinimapParamOutputRGBA, MinimapParamOutputRGBA, MinimapParamOutputRGBA, MinimapParamOutputRGBA,
		MinimapParamOutputDefault, MinimapParamOutputDefault, MinimapParamOutputDefault, MinimapParamOutputDefault,
		MinimapParamOutputDefault, MinimapParamOutputDefault, MinimapParamOutputDefault,
		MinimapParamOutputDefault, MinimapParamOutputDefault,
		MinimapParamOutputDefault, MinimapParamOutputDefault, MinimapParamOutputDefault };

	static_assert(UE_ARRAY_COUNT(Sources) == UE_ARRAY_COUNT(InputNames), "Custom node source/input arrays must stay aligned.");
	static_assert(UE_ARRAY_COUNT(Sources) == UE_ARRAY_COUNT(SourceOutputs), "Custom node source/output arrays must stay aligned.");

	UMaterialExpressionCustom* Custom = BuildCompositeNode(Material, MinimapMaterialHLSL, InputNames, Sources, SourceOutputs);
	FinalizeCompositeMaterial(Material, Custom, PackagePath);
}

void FMinimapAssetFactory::CreateDefaultAssets(bool bForceRebuildMaterial)
{
	CreateMinimapMaterial(bForceRebuildMaterial);

	// Player arrow: point-up triangle with a tail notch.
	CreateIconTexture(TEXT("TX_PlayerArrow"), [](const FVector2D& P)
	{
		const bool bInOuter = PointInTriangle(P, FVector2D(0.5, 0.04), FVector2D(0.94, 0.96), FVector2D(0.06, 0.96));
		const bool bInNotch = PointInTriangle(P, FVector2D(0.5, 0.55), FVector2D(0.82, 1.02), FVector2D(0.18, 1.02));
		return bInOuter && !bInNotch;
	});

	// Edge arrow: solid point-up chevron triangle.
	CreateIconTexture(TEXT("TX_EdgeArrow"), [](const FVector2D& P)
	{
		return PointInTriangle(P, FVector2D(0.5, 0.08), FVector2D(0.92, 0.92), FVector2D(0.08, 0.92));
	});

	// Objective marker: diamond.
	CreateIconTexture(TEXT("TX_ObjectiveMarker"), [](const FVector2D& P)
	{
		return FMath::Abs(P.X - 0.5) + FMath::Abs(P.Y - 0.5) <= 0.44;
	});

	// Default dot: filled circle.
	CreateIconTexture(TEXT("TX_DefaultDot"), [](const FVector2D& P)
	{
		return FVector2D::Distance(P, FVector2D(0.5, 0.5)) <= 0.42;
	});

	UE_LOG(LogModularMinimapAssets, Log, TEXT("Default minimap assets are ready."));
}

/** Recompile one generated material and report its shader compile errors. */
static bool ValidateGeneratedMaterial(const TCHAR* ObjectPath, const TCHAR* AssetName)
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, ObjectPath);
	if (Material == nullptr)
	{
		UE_LOG(LogModularMinimapAssets, Error, TEXT("VALIDATE: could not load %s."), AssetName);
		return false;
	}

	UMaterialEditingLibrary::RecompileMaterial(Material);

	if (GShaderCompilingManager != nullptr)
	{
		GShaderCompilingManager->FinishAllCompilation();
	}

	const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
	if (Resource == nullptr)
	{
		UE_LOG(LogModularMinimapAssets, Error, TEXT("VALIDATE: no material resource for %s."), AssetName);
		return false;
	}

	const TArray<FString>& Errors = Resource->GetCompileErrors();
	for (const FString& Error : Errors)
	{
		UE_LOG(LogModularMinimapAssets, Error, TEXT("VALIDATE: %s: %s"), AssetName, *Error);
	}

	const bool bCompiled = Errors.IsEmpty() && Resource->IsCompilationFinished();
	UE_LOG(LogModularMinimapAssets, Display, TEXT("VALIDATE_RESULT: %s %s (%d compile errors)"),
		AssetName, bCompiled ? TEXT("PASS") : TEXT("FAIL"), Errors.Num());
	return bCompiled;
}

bool FMinimapAssetFactory::ValidateDefaultMaterial()
{
	return ValidateGeneratedMaterial(TEXT("/ModularMinimap/Materials/M_MinimapBase.M_MinimapBase"), TEXT("M_MinimapBase"));
}
