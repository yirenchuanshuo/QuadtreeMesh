#include "ShaderParameterUtils.h"
#include "RHIStaticStates.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "MeshMaterialShader.h"
#include "DataDrivenShaderPlatformInfo.h"

// ----------------------------------------------------------------------------------

inline FQuadtreeMeshVertexFactory::FQuadtreeMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, int32 InNumQuadsPerSide, float InLODScale)
	: FVertexFactory(InFeatureLevel)
	, NumQuadsPerSide(InNumQuadsPerSide)
	, LODScale(InLODScale)
{
	VertexBuffer = new FQuadtreeMeshVertexBuffer(NumQuadsPerSide);
	IndexBuffer = new FQuadtreeMeshIndexBuffer(NumQuadsPerSide);
}


inline FQuadtreeMeshVertexFactory::~FQuadtreeMeshVertexFactory()
{
	delete VertexBuffer;
	delete IndexBuffer;
}


inline void FQuadtreeMeshVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
{
	Super::InitRHI(RHICmdList);

	// Setup the uniform data:
	SetupUniformDataForGroup(EQuadtreeMeshRenderGroupType::RG_RenderQuadtreeMeshTiles);

	SetupUniformDataForGroup(EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly);
	SetupUniformDataForGroup(EQuadtreeMeshRenderGroupType::RG_RenderUnselectedQuadtreeMeshTilesOnly);


	VertexBuffer->InitResource(RHICmdList);
	IndexBuffer->InitResource(RHICmdList);

	check(Streams.Num() == 0);

	FVertexStream PositionVertexStream;
	PositionVertexStream.VertexBuffer = VertexBuffer;
	PositionVertexStream.Stride = sizeof(FVector4f);
	PositionVertexStream.Offset = 0;
	PositionVertexStream.VertexStreamUsage = EVertexStreamUsage::Default;

	// Simple instancing vertex stream with nullptr vertex buffer to be set at binding time
	FVertexStream InstanceDataVertexStream;
	InstanceDataVertexStream.VertexBuffer = nullptr;
	InstanceDataVertexStream.Stride = sizeof(FVector4f);
	InstanceDataVertexStream.Offset = 0;
	InstanceDataVertexStream.VertexStreamUsage = EVertexStreamUsage::Instancing;

	FVertexElement VertexPositionElement(Streams.Add(PositionVertexStream), 0, VET_Float4, 0, PositionVertexStream.Stride, false);

	// Vertex declaration
	FVertexDeclarationElementList Elements;
	Elements.Add(VertexPositionElement);

	// Adds all streams
	for (int32 StreamIdx = 0; StreamIdx < NumAdditionalVertexStreams; ++StreamIdx)
	{
		FVertexElement InstanceElement(Streams.Add(InstanceDataVertexStream), 0, VET_Float4, 8 + StreamIdx, InstanceDataVertexStream.Stride, true);
		Elements.Add(InstanceElement);
	}

	InitDeclaration(Elements);
}


inline void FQuadtreeMeshVertexFactory::ReleaseRHI()
{
	for (auto& UniformBuffer : UniformBuffers)
	{
		UniformBuffer.SafeRelease();
	}

	if (VertexBuffer)
	{
		VertexBuffer->ReleaseResource();
	}

	if (IndexBuffer)
	{
		IndexBuffer->ReleaseResource();
	}

	Super::ReleaseRHI();
}


inline void FQuadtreeMeshVertexFactory::SetupUniformDataForGroup(EQuadtreeMeshRenderGroupType InRenderGroupType)
{
	FQuadtreeMeshVertexFactoryParameters UniformParams;
	UniformParams.NumQuadsPerTileSide = NumQuadsPerSide;
	UniformParams.LODScale = LODScale;
	UniformParams.bRenderSelected = true;
	UniformParams.bRenderUnselected = true;
	UniformParams.bRenderSelected = (InRenderGroupType != EQuadtreeMeshRenderGroupType::RG_RenderUnselectedQuadtreeMeshTilesOnly);
	UniformParams.bRenderUnselected = (InRenderGroupType != EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly);
	UniformBuffers[static_cast<int32>(InRenderGroupType)] = FQuadtreeMeshVertexFactoryBufferRef::CreateUniformBufferImmediate(UniformParams, UniformBuffer_MultiFrame);
}


inline bool FQuadtreeMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	const bool bIsCompatibleWithQuadtreeMesh = Parameters.MaterialParameters.MaterialDomain == MD_Surface || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
	if (bIsCompatibleWithQuadtreeMesh)
	{
		return (IsPCPlatform(Parameters.Platform));
	}
	return false;
}


inline void FQuadtreeMeshVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("QUADTREE_MESH_FACTORY"), 1);
	OutEnvironment.SetDefine(TEXT("USE_VERTEXFACTORY_HITPROXY_ID"), TEXT("1"));
	OutEnvironment.SetDefine(TEXT("RAY_TRACING_DYNAMIC_MESH_IN_LOCAL_SPACE"), TEXT("1"));
}


inline void FQuadtreeMeshVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements)
{
	// Add position stream
	Elements.Add(FVertexElement(0, 0, VET_Float4, 0, sizeof(FVector4f), false));

	// Add all the additional streams
	for (int32 StreamIdx = 0; StreamIdx < NumAdditionalVertexStreams; ++StreamIdx)
	{
		Elements.Add(FVertexElement(1 + StreamIdx, 0, VET_Float4, 8 + StreamIdx, sizeof(FVector4f), true));
	}
}