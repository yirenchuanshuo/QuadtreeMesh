/*
#include "QuadtreeMeshRender.h"
#include "LegacyScreenPercentageDriver.h"
#include "Modules/ModuleManager.h"
#include "RenderCaptureInterface.h"
#include "RHIStaticStates.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/OrthoMatrix.h"
#include "GameFramework/WorldSettings.h"
#include "LandscapeRender.h"
#include "TextureResource.h"
#include "QuadtreeMeshComponent.h"
#include "Runtime/Renderer/Private/SceneRendering.h"



namespace UE::QuadtreeMeshInfo
{

	
	static FMatrix BuildOrthoMatrix(float InOrthoWidth, float InOrthoHeight)
	{
		check(static_cast<int32>(ERHIZBuffer::IsInverted));

		const FMatrix::FReal OrthoWidth = InOrthoWidth / 2.0f;
		const FMatrix::FReal OrthoHeight = InOrthoHeight / 2.0f;

		const FMatrix::FReal NearPlane = 0.f;
		const FMatrix::FReal FarPlane = UE_FLOAT_HUGE_DISTANCE / 4.0f;

		const FMatrix::FReal ZScale = 1.0f / (FarPlane - NearPlane);
		const FMatrix::FReal ZOffset = 0;

		return FReversedZOrthoMatrix(
			OrthoWidth,
			OrthoHeight,
			ZScale,
			ZOffset
			);
	}

	
	struct FCreateQuadtreeMeshInfoSceneRendererParams
	{
public:
		FCreateQuadtreeMeshInfoSceneRendererParams(const FRenderingContext& InContext):Context(InContext){}

		const FRenderingContext& Context;
		
		FSceneInterface* Scene = nullptr;
		FRenderTarget* RenderTarget = nullptr;

		FIntPoint RenderTargetSize = FIntPoint(EForceInit::ForceInit);
		FMatrix ViewRotationMatrix = FMatrix(EForceInit::ForceInit);
		FVector ViewLocation = FVector::Zero();
		FMatrix ProjectionMatrix = FMatrix(EForceInit::ForceInit);
		ESceneCaptureSource CaptureSource = ESceneCaptureSource::SCS_MAX;
		TSet<FPrimitiveComponentId> ShowOnlyPrimitives;
	};

	static FSceneRenderer* CreateQuadtreeMeshInfoSceneRenderer(const FCreateQuadtreeMeshInfoSceneRendererParams& Params)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(QuadtreeMeshInfo::CreateQuadtreeMeshInfoRenderer);

		check(Params.Scene != nullptr)
		check(Params.RenderTarget != nullptr)
		check(Params.CaptureSource != ESceneCaptureSource::SCS_MAX)

		FEngineShowFlags ShowFlags(ESFIM_Game);
		ShowFlags.NaniteMeshes = 0;
		ShowFlags.Atmosphere = 0;
		ShowFlags.Lighting = 0;
		ShowFlags.Bloom = 0;
		ShowFlags.ScreenPercentage = 0;
		ShowFlags.Translucency = 0;
		ShowFlags.SeparateTranslucency = 0;
		ShowFlags.AntiAliasing = 0;
		ShowFlags.Fog = 0;
		ShowFlags.VolumetricFog = 0;
		ShowFlags.DynamicShadows = 0;

		ShowFlags.SetDisableOcclusionQueries(true);
		ShowFlags.SetVirtualShadowMapCaching(false);
		
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
			Params.RenderTarget,
			Params.Scene,
			ShowFlags)
			.SetRealtimeUpdate(false)
			.SetResolveScene(false));
		ViewFamily.SceneCaptureSource = Params.CaptureSource;

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, Params.RenderTargetSize.X, Params.RenderTargetSize.Y));
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.ViewRotationMatrix = Params.ViewRotationMatrix;
		ViewInitOptions.ViewOrigin = Params.ViewLocation;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverrideFarClippingPlaneDistance = -1.f;
		ViewInitOptions.SceneViewStateInterface = nullptr;
		ViewInitOptions.ProjectionMatrix = Params.ProjectionMatrix;
		ViewInitOptions.LODDistanceFactor = 0.001f;
		ViewInitOptions.OverlayColor = FLinearColor::Black;
		// Must be set to false to prevent the renders from using different VSM page pool sizes leading to unnecessary reallocations.
		ViewInitOptions.bIsSceneCapture = false;

		if (ViewFamily.Scene->GetWorld() != nullptr && ViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
		{
			ViewInitOptions.WorldToMetersScale = ViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
		}

		FSceneView* View = new FSceneView(ViewInitOptions);
		View->GPUMask = FRHIGPUMask::All();
		View->bOverrideGPUMask = true;
		View->AntiAliasingMethod = EAntiAliasingMethod::AAM_None;
		View->SetupAntiAliasingMethod();

		View->ShowOnlyPrimitives = Params.ShowOnlyPrimitives;

		ViewFamily.Views.Add(View);

		View->StartFinalPostprocessSettings(Params.ViewLocation);
		View->EndFinalPostprocessSettings(ViewInitOptions);

		ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ViewFamily, 1.f));

		ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(ViewFamily.Scene));
		for (const FSceneViewExtensionRef& Extension : ViewFamily.ViewExtensions)
		{
			Extension->SetupViewFamily(ViewFamily);
			Extension->SetupView(ViewFamily, *View);
		}

		return FSceneRenderer::CreateSceneRenderer(&ViewFamily, nullptr);
	}

	
	void UpdateQuadtreeMeshInfoRendering(FSceneInterface* Scene, const FRenderingContext& Context)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(QuadtreeMeshInfo::UpdateQuadtreeMeshInfoRendering);
	
		RenderCaptureInterface::FScopedCapture RenderCapture(false, TEXT("RenderQuadtreeMeshInfo"));

		if (Scene == nullptr)
		{
			return;
		}
		
		const FVector QuadtreeMeshExtent = Context.QuadtreeMeshToRender->GetDynamicQuadtreeMeshExtent();
		FVector ViewLocation = Context.QuadtreeMeshToRender->GetDynamicQuadtreeMeshExtent();
		ViewLocation.Z = Context.CaptureZ;

		const FBox2D CaptureBounds(FVector2D(ViewLocation - QuadtreeMeshExtent), FVector2D(ViewLocation + QuadtreeMeshExtent));

		// Zone rendering always happens facing towards negative z.
		const FVector LookAt = ViewLocation - FVector(0.f, 0.f, 1.f);
		
		const FIntPoint CaptureExtent(Context.TextureRenderTarget->GetSurfaceWidth(), Context.TextureRenderTarget->GetSurfaceHeight());

		// Initialize the generic parameters which are passed to each of the scene renderers
		FCreateQuadtreeMeshInfoSceneRendererParams CreateSceneRendererParams(Context);
		CreateSceneRendererParams.Scene = Scene;
		CreateSceneRendererParams.RenderTarget = Context.TextureRenderTarget->GameThread_GetRenderTargetResource();
		CreateSceneRendererParams.RenderTargetSize = CaptureExtent;
		CreateSceneRendererParams.ViewLocation = ViewLocation;
		CreateSceneRendererParams.ProjectionMatrix = BuildOrthoMatrix(QuadtreeMeshExtent.X, QuadtreeMeshExtent.Y);
		CreateSceneRendererParams.ViewRotationMatrix = FLookAtMatrix(ViewLocation, LookAt, FVector(0.f, -1.f, 0.f));
		CreateSceneRendererParams.ViewRotationMatrix = CreateSceneRendererParams.ViewRotationMatrix.RemoveTranslation();
		CreateSceneRendererParams.ViewRotationMatrix.RemoveScaling();

		TSet<FPrimitiveComponentId> ComponentsToRenderInDepthPass;
		
		CreateSceneRendererParams.CaptureSource = SCS_DeviceDepth;
		CreateSceneRendererParams.ShowOnlyPrimitives = MoveTemp(ComponentsToRenderInDepthPass);
		FSceneRenderer* DepthRenderer = CreateQuadtreeMeshInfoSceneRenderer(CreateSceneRendererParams);
		
		TSet<FPrimitiveComponentId> ComponentsToRenderInColorPass;
		TSet<FPrimitiveComponentId> ComponentsToRenderInDilationPass;
		
		ComponentsToRenderInColorPass.Reserve(1);
		ComponentsToRenderInDilationPass.Reserve(1);
				

		// Perform our own simple culling based on the known Capture bounds:
		const FBox QuadtreeMeshBodyBounds = Context.QuadtreeMeshToRender->Bounds.GetBox();
		if (CaptureBounds.Intersect(FBox2D(FVector2D(QuadtreeMeshBodyBounds.Min), FVector2D(QuadtreeMeshBodyBounds.Max))))
		{
			ComponentsToRenderInColorPass.Add( Context.QuadtreeMeshToRender->ComponentId);
			ComponentsToRenderInDilationPass.Add( Context.QuadtreeMeshToRender->ComponentId);
		}
			
		
		CreateSceneRendererParams.CaptureSource = SCS_SceneColorSceneDepth;
		CreateSceneRendererParams.ShowOnlyPrimitives = MoveTemp(ComponentsToRenderInColorPass);
		FSceneRenderer* ColorRenderer = CreateQuadtreeMeshInfoSceneRenderer(CreateSceneRendererParams);

		CreateSceneRendererParams.CaptureSource = SCS_DeviceDepth;
		CreateSceneRendererParams.ShowOnlyPrimitives = MoveTemp(ComponentsToRenderInDilationPass);
		FSceneRenderer* DilationRenderer = CreateQuadtreeMeshInfoSceneRenderer(CreateSceneRendererParams);

		FTextureRenderTargetResource* TextureRenderTargetResource = Context.TextureRenderTarget->GameThread_GetRenderTargetResource();

		/*FUpdateWaterInfoParams Params;
		Params.Scene = Scene;
		Params.DepthRenderer = DepthRenderer;
		Params.ColorRenderer = ColorRenderer;
		Params.DilationRenderer = DilationRenderer;
		Params.RenderTarget = TextureRenderTargetResource;
		Params.OutputTexture = TextureRenderTargetResource;
		Params.CaptureZ = ViewLocation.Z;
		Params.WaterHeightExtents = Context.QuadtreeMeshToRender->GetWaterHeightExtents();
		Params.GroundZMin = Context.QuadtreeMeshToRender->GetGroundZMin();
		Params.VelocityBlurRadius = Context.QuadtreeMeshToRender->GetVelocityBlurRadius();
		Params.WaterZoneExtents = QuadtreeMeshExtent;

		ENQUEUE_RENDER_COMMAND(QuadtreeMeshInfoCommand)(
		[Params, QuadtreeMeshName = Context.QuadtreeMeshToRender->GetName()](FRHICommandListImmediate& RHICmdList)
			{
				SCOPED_DRAW_EVENTF(RHICmdList, QuadtreeMeshInfoRendering_RT, TEXT("RenderQuadtreeMeshInfo_%s"), *QuadtreeMeshName);

				UpdateQuadtreeMeshInfoRendering_RenderThread(RHICmdList, Params);
			});#1#
	}
}

FQuadtreeMeshViewExtension::FQuadtreeMeshViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld)
	:FWorldSceneViewExtension(AutoReg, InWorld)
{
	
}

FQuadtreeMeshViewExtension::~FQuadtreeMeshViewExtension()
{
	
}

void FQuadtreeMeshViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
	
	const TWeakObjectPtr<UWorld> WorldPtr = GetWorld();
	check(WorldPtr.IsValid())

	static bool bUpdatingQuadtreeMeshInfo = false;
	if (!bUpdatingQuadtreeMeshInfo)
	{
		bUpdatingQuadtreeMeshInfo = true;

		//const UE::QuadtreeMeshInfo::FRenderingContext& Context(RenderContext);
		//UpdateQuadtreeMeshInfoRendering(WorldPtr.Get()->Scene, Context);

		bUpdatingQuadtreeMeshInfo = false;
	}
	
}

void FQuadtreeMeshViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FQuadtreeMeshViewExtension::PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder,
	FSceneViewFamily& InViewFamily)
{
	
}
*/


