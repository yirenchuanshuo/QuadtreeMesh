#include "QuadtreeMeshRender.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "QuadtreeMeshComponent.h"
#include "PostProcess/DrawRectangle.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"


namespace UE::QuadtreeMeshInfo
{
	struct FUpdateQuadtreeMeshInfoParams
	{
		FSceneInterface* Scene = nullptr;
		FSceneRenderer* DepthRenderer = nullptr;
		FSceneRenderer* ColorRenderer = nullptr;
		FSceneRenderer* DilationRenderer = nullptr;
		FRenderTarget* RenderTarget = nullptr;
		FTexture* OutputTexture = nullptr;

		FVector WorldPosition;
		FVector2f MeshHeightExtents;
		float GroundZMin;
		float CaptureZ;
		int32 VelocityBlurRadius;
	};

	class FQuadtreeMeshInfoMergePS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FQuadtreeMeshInfoMergePS);
		SHADER_USE_PARAMETER_STRUCT(FQuadtreeMeshInfoMergePS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
			SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DepthTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, DepthTextureSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ColorTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, ColorTextureSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, DilationTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, DilationTextureSampler)
			SHADER_PARAMETER(FVector2f, MeshHeightExtents)
			SHADER_PARAMETER(float, GroundZMin)
			SHADER_PARAMETER(float, CaptureZ)
			SHADER_PARAMETER(float, UndergroundDilationDepthOffset)
			SHADER_PARAMETER(float, DilationOverwriteMinimumDistance)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			// Water info merge unconditionally requires a 128 bit render target. Some platforms require explicitly enabling this output mode.
			bool bPlatformRequiresExplicit128bitRT = FDataDrivenShaderPlatformInfo::GetRequiresExplicit128bitRT(Parameters.Platform);
			if (bPlatformRequiresExplicit128bitRT)
			{
				OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
			}
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FQuadtreeMeshInfoMergePS, "/Plugin/QuadtreeMesh/Private/QuadtreeMeshInfoMerge.usf", "Main", SF_Pixel);

	static void MergeQuadMeshInfoAndDepth(
	FRDGBuilder& GraphBuilder,
	const FSceneViewFamily& ViewFamily,
	const FSceneView& View,
	FRDGTextureRef OutputTexture,
	FRDGTextureRef DepthTexture,
	FRDGTextureRef ColorTexture,
	FRDGTextureRef DilationTexture,
	const FUpdateQuadtreeMeshInfoParams& Params)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "QuadtreeMeshInfoDepthMerge");

		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

		FQuadtreeMeshInfoMergePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FQuadtreeMeshInfoMergePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);
		PassParameters->SceneTextures = GetSceneTextureShaderParameters(View);
		PassParameters->DepthTexture = DepthTexture;
		PassParameters->DepthTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->ColorTexture = ColorTexture;
		PassParameters->ColorTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->DilationTexture = DilationTexture;
		PassParameters->DilationTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->CaptureZ = Params.CaptureZ;
		PassParameters->MeshHeightExtents = Params.MeshHeightExtents;
		PassParameters->GroundZMin = Params.GroundZMin;
		PassParameters->DilationOverwriteMinimumDistance = 128.f;
		PassParameters->UndergroundDilationDepthOffset =64.f;

		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FQuadtreeMeshInfoMergePS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("QuadtreeMeshInfoDepthMerge"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, GraphicsPSOInit, VertexShader, PixelShader, &View](FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer LocalGraphicsPSOInit = GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(LocalGraphicsPSOInit);
				SetGraphicsPipelineState(RHICmdList, LocalGraphicsPSOInit, 0);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				UE::Renderer::PostProcess::DrawRectangle(RHICmdList, VertexShader, View, EDRF_UseTriangleOptimization);
			});
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

		const UE::QuadtreeMeshInfo::FRenderingContext& Context(RenderContext);
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


