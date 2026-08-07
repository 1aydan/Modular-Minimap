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

static const TCHAR* MinimapMaterialHLSL = TEXT(R"HLSL(
// Base is widget-normalized (edges at +-0.5 on both axes) and drives the mask; Local is corrected
// so one unit means the same number of pixels on both axes, matching how the icon layer projects.
float2 Base = UV - 0.5f;
float2 Local = Base * AspectScale.rg;
float S = sin(RotationRad);
float C = cos(RotationRad);
float2 MapUV = CenterUV.rg + float2(Local.x * C - Local.y * S, Local.x * S + Local.y * C) * UVSpan;

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
float Fill = MapSample.r;
float TexelOffset = OutlineTexels / max(MapTexelCount, 1.0f);
float N0 = Texture2DSample(MapTex, MapTexSampler, clamp(SampleUV + float2(TexelOffset, 0.0f), 0.0f, 1.0f)).r;
float N1 = Texture2DSample(MapTex, MapTexSampler, clamp(SampleUV - float2(TexelOffset, 0.0f), 0.0f, 1.0f)).r;
float N2 = Texture2DSample(MapTex, MapTexSampler, clamp(SampleUV + float2(0.0f, TexelOffset), 0.0f, 1.0f)).r;
float N3 = Texture2DSample(MapTex, MapTexSampler, clamp(SampleUV - float2(0.0f, TexelOffset), 0.0f, 1.0f)).r;
float Outline = saturate(max(max(N0, N1), max(N2, N3)) - Fill);
BaseColor = FillColor.rgb * Fill + OutlineColor.rgb * Outline;
BaseAlpha = saturate(Fill * FillColor.a + Outline * OutlineColor.a);
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

static void CreateMinimapMaterial(bool bForce)
{
	const FString PackagePath = TEXT("/ModularMinimap/Materials/M_MinimapBase");

	UMaterial* Material = nullptr;
	if (FPackageName::DoesPackageExist(PackagePath))
	{
		if (!bForce)
		{
			UE_LOG(LogModularMinimapAssets, Log, TEXT("%s already exists; skipping (pass 'force' to rebuild)."), *PackagePath);
			return;
		}

		// Rebuild the existing asset in place so referencing content keeps working.
		Material = LoadObject<UMaterial>(nullptr, *(PackagePath + TEXT(".M_MinimapBase")));
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

			// Not UMaterialEditingLibrary::DeleteAllMaterialExpressions: it range-iterates the
			// material's expression array while DeleteMaterialExpression removes entries from that
			// same array, so it skips every other expression and each rebuild leaves orphans behind.
			TArray<UMaterialExpression*> ExistingExpressions;
			ExistingExpressions.Reserve(Material->GetExpressions().Num());
			for (UMaterialExpression* Expression : Material->GetExpressions())
			{
				ExistingExpressions.Add(Expression);
			}

			for (UMaterialExpression* Expression : ExistingExpressions)
			{
				UMaterialEditingLibrary::DeleteMaterialExpression(Material, Expression);
			}

			UE_LOG(LogModularMinimapAssets, Log, TEXT("Rebuilding %s (cleared %d expressions)."), *PackagePath, ExistingExpressions.Num());
		}
	}

	if (Material == nullptr)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		Material = Cast<UMaterial>(Factory->FactoryCreateNew(
			UMaterial::StaticClass(), Package, TEXT("M_MinimapBase"), RF_Public | RF_Standalone, nullptr, GWarn));
	}

	if (Material == nullptr)
	{
		UE_LOG(LogModularMinimapAssets, Warning, TEXT("Failed to create %s."), *PackagePath);
		return;
	}

	Material->MaterialDomain = MD_UI;
	Material->BlendMode = BLEND_Translucent;

	UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));

	auto MakeTextureParam = [&](const FName Name, int32 PosY) -> UMaterialExpression*
	{
		UMaterialExpressionTextureObjectParameter* Param = Cast<UMaterialExpressionTextureObjectParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureObjectParameter::StaticClass(), -1100, PosY));
		Param->ParameterName = Name;
		Param->Texture = WhiteTexture;
		Param->SamplerType = SAMPLERTYPE_LinearColor;
		return Param;
	};

	auto MakeScalarParam = [&](const FName Name, float Default, int32 PosY) -> UMaterialExpression*
	{
		UMaterialExpressionScalarParameter* Param = Cast<UMaterialExpressionScalarParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -1100, PosY));
		Param->ParameterName = Name;
		Param->DefaultValue = Default;
		return Param;
	};

	auto MakeVectorParam = [&](const FName Name, const FLinearColor& Default, int32 PosY) -> UMaterialExpression*
	{
		UMaterialExpressionVectorParameter* Param = Cast<UMaterialExpressionVectorParameter>(
			UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionVectorParameter::StaticClass(), -1100, PosY));
		Param->ParameterName = Name;
		Param->DefaultValue = Default;
		return Param;
	};

	const UMinimapLevelSettings* StyleDefaults = GetDefault<UMinimapLevelSettings>();

	UMaterialExpression* TexCoord = UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -1100, -500);
	UMaterialExpression* MapTex = MakeTextureParam(TEXT("MapTexture"), -400);
	UMaterialExpression* FogE = MakeTextureParam(TEXT("FogExplored"), -250);
	UMaterialExpression* FogV = MakeTextureParam(TEXT("FogVisible"), -100);
	// Vector params carry a colour swatch and need ~150 of vertical room; scalars need ~70.
	UMaterialExpression* CenterUV = MakeVectorParam(TEXT("CenterUV"), FLinearColor(0.5f, 0.5f, 0.0f, 0.0f), 50);
	UMaterialExpression* AspectScale = MakeVectorParam(TEXT("AspectScale"), FLinearColor(1.0f, 1.0f, 0.0f, 0.0f), 200);
	UMaterialExpression* FillColor = MakeVectorParam(TEXT("FillColor"), StyleDefaults->WalkableFillColor, 350);
	UMaterialExpression* OutlineColor = MakeVectorParam(TEXT("OutlineColor"), StyleDefaults->OutlineColor, 500);
	UMaterialExpression* VoidColor = MakeVectorParam(TEXT("VoidColor"), FLinearColor(0.02f, 0.02f, 0.03f, 0.35f), 650);
	UMaterialExpression* UVSpan = MakeScalarParam(TEXT("UVSpan"), 1.0f, 800);
	UMaterialExpression* RotationRad = MakeScalarParam(TEXT("RotationRad"), 0.0f, 870);
	UMaterialExpression* MaskShape = MakeScalarParam(TEXT("MaskShape"), 1.0f, 940);
	UMaterialExpression* MapTexelCount = MakeScalarParam(TEXT("MapTexelCount"), 1024.0f, 1010);
	UMaterialExpression* OutlineTexels = MakeScalarParam(TEXT("OutlineTexels"), StyleDefaults->OutlineThickness, 1080);
	UMaterialExpression* ExploredDim = MakeScalarParam(TEXT("ExploredDim"), 0.45f, 1150);
	UMaterialExpression* FogEnabled = MakeScalarParam(TEXT("FogEnabled"), 0.0f, 1220);
	UMaterialExpression* BackgroundIsMask = MakeScalarParam(TEXT("BackgroundIsMask"), 1.0f, 1290);

	UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(
		UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), -500, 0));
	Custom->Code = MinimapMaterialHLSL;
	Custom->OutputType = CMOT_Float4;
	Custom->Description = TEXT("MinimapComposite");
	Custom->Inputs.Empty();

	const TCHAR* InputNames[] = {
		TEXT("UV"), TEXT("MapTex"), TEXT("FogE"), TEXT("FogV"), TEXT("CenterUV"), TEXT("AspectScale"),
		TEXT("FillColor"), TEXT("OutlineColor"), TEXT("VoidColor"),
		TEXT("UVSpan"), TEXT("RotationRad"), TEXT("MaskShape"), TEXT("MapTexelCount"), TEXT("OutlineTexels"),
		TEXT("ExploredDim"), TEXT("FogEnabled"), TEXT("BackgroundIsMask") };
	for (const TCHAR* InputName : InputNames)
	{
		FCustomInput Input;
		Input.InputName = InputName;
		Custom->Inputs.Add(Input);
	}

	UMaterialExpression* Sources[] = {
		TexCoord, MapTex, FogE, FogV, CenterUV, AspectScale,
		FillColor, OutlineColor, VoidColor,
		UVSpan, RotationRad, MaskShape, MapTexelCount, OutlineTexels,
		ExploredDim, FogEnabled, BackgroundIsMask };

	// A vector parameter's default output is RGB (float3); the HLSL reads .a, so those
	// must connect through the explicit RGBA output.
	const TCHAR* RGBA = TEXT("RGBA");
	const TCHAR* SourceOutputs[] = {
		TEXT(""), TEXT(""), TEXT(""), TEXT(""), RGBA, RGBA,
		RGBA, RGBA, RGBA,
		TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT(""),
		TEXT(""), TEXT(""), TEXT("") };

	static_assert(UE_ARRAY_COUNT(Sources) == UE_ARRAY_COUNT(InputNames), "Custom node source/input arrays must stay aligned.");
	static_assert(UE_ARRAY_COUNT(Sources) == UE_ARRAY_COUNT(SourceOutputs), "Custom node source/output arrays must stay aligned.");

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sources); ++Index)
	{
		if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Sources[Index], SourceOutputs[Index], Custom, InputNames[Index]))
		{
			UE_LOG(LogModularMinimapAssets, Warning, TEXT("Failed to connect custom node input '%s'."), InputNames[Index]);
		}
	}

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

bool FMinimapAssetFactory::ValidateDefaultMaterial()
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, TEXT("/ModularMinimap/Materials/M_MinimapBase.M_MinimapBase"));
	if (Material == nullptr)
	{
		UE_LOG(LogModularMinimapAssets, Error, TEXT("VALIDATE: could not load M_MinimapBase."));
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
		UE_LOG(LogModularMinimapAssets, Error, TEXT("VALIDATE: no material resource for M_MinimapBase."));
		return false;
	}

	const TArray<FString>& Errors = Resource->GetCompileErrors();
	for (const FString& Error : Errors)
	{
		UE_LOG(LogModularMinimapAssets, Error, TEXT("VALIDATE: %s"), *Error);
	}

	const bool bCompiled = Errors.IsEmpty() && Resource->IsCompilationFinished();
	UE_LOG(LogModularMinimapAssets, Display, TEXT("VALIDATE_RESULT: %s (%d compile errors)"),
		bCompiled ? TEXT("PASS") : TEXT("FAIL"), Errors.Num());
	return bCompiled;
}
