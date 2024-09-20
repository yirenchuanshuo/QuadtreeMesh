#include "QuadtreeMeshVertexFactory.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "MaterialDomain.h"
#include "MeshDrawShaderBindings.h"
#include "MeshBatch.h"
#include "MeshMaterialShader.h"
#include "RenderUtils.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FQuadtreeMeshVertexFactoryParameters, "QuadtreeMeshVF");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FQuadtreeMeshVertexFactoryRaytracingParameters, "QuadtreeMeshRaytracingVF");


class FQuadtreeMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(FQuadtreeMeshVertexFactoryShaderParameters, NonVirtual);
public:

	void Bind(const FShaderParameterMap& ParameterMap)
	{
	}

	void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const class FSceneView* View,
		const class FMeshMaterialShader* Shader,
		const EVertexInputStreamType InputStreamType,
		ERHIFeatureLevel::Type FeatureLevel,
		const class FVertexFactory* InVertexFactory,
		const struct FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams) const
	{
		const FQuadtreeMeshVertexFactory* VertexFactory =  static_cast<const FQuadtreeMeshVertexFactory*>(InVertexFactory);

		const FQuadtreeMeshUserData* QuadtreeMeshUserData = static_cast<const FQuadtreeMeshUserData*>(BatchElement.UserData);

		const  FQuadtreeMeshInstanceDataBuffers* InstanceDataBuffers = QuadtreeMeshUserData->InstanceDataBuffers;

		

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FQuadtreeMeshVertexFactoryParameters>(), VertexFactory->GeFQuadtreeMeshVertexFactoryUniformBuffer(QuadtreeMeshUserData->RenderGroupType));

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FQuadtreeMeshVertexFactoryRaytracingParameters>(), QuadtreeMeshUserData->QuadtreeMeshVertexFactoryRaytracingVFUniformBuffer);
		}
#endif

		if (VertexStreams.Num() > 0)
		{
			for (int32 i = 0; i < FQuadtreeMeshInstanceDataBuffers::NumBuffers; ++i)
			{
				FVertexInputStream* InstanceInputStream = VertexStreams.FindByPredicate([i](const FVertexInputStream& InStream) { return InStream.StreamIndex == i+1; });
				check(InstanceInputStream);
				
				// Bind vertex buffer
				check(InstanceDataBuffers->GetBuffer(i));
				InstanceInputStream->VertexBuffer = InstanceDataBuffers->GetBuffer(i);
			}
			const int32 InstanceOffsetValue = BatchElement.UserIndex;
			if (InstanceOffsetValue > 0)
			{
				VertexFactory->OffsetInstanceStreams(InstanceOffsetValue, InputStreamType, VertexStreams);
			}
		}
	}
};

FQuadtreeMeshVertexFactory::FQuadtreeMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, int32 InNumQuadsPerSide, float InLODScale)
	: FVertexFactory(InFeatureLevel)
	, NumQuadsPerSide(InNumQuadsPerSide)
	, LODScale(InLODScale)
{
	VertexBuffer = new FQuadtreeMeshVertexBuffer(NumQuadsPerSide);
	IndexBuffer = new FQuadtreeMeshIndexBuffer(NumQuadsPerSide);
}


FQuadtreeMeshVertexFactory::~FQuadtreeMeshVertexFactory()
{
	delete VertexBuffer;
	delete IndexBuffer;
}


void FQuadtreeMeshVertexFactory::InitRHI(FRHICommandListBase& RHICmdList)
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
	
	FVertexElement VertexPositionElement(Streams.Add(PositionVertexStream), 0, VET_Float4, 0, PositionVertexStream.Stride, false);

	// Vertex declaration
	FVertexDeclarationElementList Elements;
	Elements.Add(VertexPositionElement);

	if constexpr(NumAdditionalVertexStreams>0)
	{
		// Simple instancing vertex stream with nullptr vertex buffer to be set at binding time
		FVertexStream InstanceDataVertexStream;
		InstanceDataVertexStream.VertexBuffer = nullptr;
		InstanceDataVertexStream.Stride = sizeof(FVector4f);
		InstanceDataVertexStream.Offset = 0;
		InstanceDataVertexStream.VertexStreamUsage = EVertexStreamUsage::Instancing;

		// Adds all streams
	
		for (int32 StreamIdx = 0; StreamIdx < NumAdditionalVertexStreams; ++StreamIdx)
		{
			FVertexElement InstanceElement(Streams.Add(InstanceDataVertexStream), 0, VET_Float4, 8 + StreamIdx, InstanceDataVertexStream.Stride, true);
			Elements.Add(InstanceElement);
		}
	}
	InitDeclaration(Elements);
}


void FQuadtreeMeshVertexFactory::ReleaseRHI()
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


void FQuadtreeMeshVertexFactory::SetupUniformDataForGroup(EQuadtreeMeshRenderGroupType InRenderGroupType)
{
	FQuadtreeMeshVertexFactoryParameters UniformParams;
	UniformParams.NumQuadsPerTileSide = NumQuadsPerSide;
	UniformParams.LODScale = LODScale;
	UniformParams.bRenderSelected = (InRenderGroupType != EQuadtreeMeshRenderGroupType::RG_RenderUnselectedQuadtreeMeshTilesOnly);
	UniformParams.bRenderUnselected = (InRenderGroupType != EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly);
	UniformBuffers[static_cast<int32>(InRenderGroupType)] = FQuadtreeMeshVertexFactoryBufferRef::CreateUniformBufferImmediate(UniformParams, UniformBuffer_MultiFrame);
}


bool FQuadtreeMeshVertexFactory::ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters)
{
	const bool bIsCompatibleWithQuadtreeMesh = Parameters.MaterialParameters.MaterialDomain == MD_Surface || Parameters.MaterialParameters.bIsSpecialEngineMaterial;
	if (bIsCompatibleWithQuadtreeMesh)
	{
		return IsPCPlatform(Parameters.Platform);
	}
	return false;
}


void FQuadtreeMeshVertexFactory::ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutEnvironment.SetDefine(TEXT("QUADTREE_MESH_FACTORY"), 1);
	OutEnvironment.SetDefine(TEXT("USE_VERTEXFACTORY_HITPROXY_ID"), TEXT("1"));
	OutEnvironment.SetDefine(TEXT("RAY_TRACING_DYNAMIC_MESH_IN_LOCAL_SPACE"), TEXT("1"));
}




void FQuadtreeMeshVertexFactory::GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements)
{
	// Add position stream
	Elements.Add(FVertexElement(0, 0, VET_Float4, 0, sizeof(FVector4f), false));

	// Add all the additional streams
	if constexpr(NumAdditionalVertexStreams>0)
	{
		for (int32 StreamIdx = 0; StreamIdx < NumAdditionalVertexStreams; ++StreamIdx)
		{
			Elements.Add(FVertexElement(1 + StreamIdx, 0, VET_Float4, 8 + StreamIdx, sizeof(FVector4f), true));
		}
	}
	
}

void FQuadtreeMeshVertexFactory::ValidateCompiledResult(const FVertexFactoryType* Type, EShaderPlatform Platform,
	const FShaderParameterMap& ParameterMap, TArray<FString>& OutErrors)
{
	/*if (Type->SupportsPrimitiveIdStream()
		&& UseGPUScene(Platform, GetMaxSupportedFeatureLevel(Platform))
		&& ParameterMap.ContainsParameterAllocation(FPrimitiveUniformShaderParameters::FTypeInfo::GetStructMetadata()->GetShaderVariableName()))
	{
		OutErrors.AddUnique(*FString::Printf(TEXT("Shader attempted to bind the Primitive uniform buffer even though Vertex Factory %s computes a PrimitiveId per-instance.  This will break auto-instancing.  Shaders should use GetPrimitiveData(Parameters).Member instead of Primitive.Member."), Type->GetName()));
	}*/
};


// ----------------------------------------------------------------------------------

// Always implement the basic vertex factory so that it's there for both editor and non-editor builds :



// In editor builds, also implement the vertex factory that supports water selection:
IMPLEMENT_TYPE_LAYOUT(FQuadtreeMeshVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FQuadtreeMeshVertexFactory, SF_Vertex, FQuadtreeMeshVertexFactoryShaderParameters);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FQuadtreeMeshVertexFactory, SF_Compute, FQuadtreeMeshVertexFactoryShaderParameters);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(FQuadtreeMeshVertexFactory, SF_RayHitGroup, FQuadtreeMeshVertexFactoryShaderParameters);
#endif
IMPLEMENT_VERTEX_FACTORY_TYPE(FQuadtreeMeshVertexFactory, "/Plugin/QuadtreeMesh/Private/QuadtreeMeshVertexFactory.ush",
	EVertexFactoryFlags::UsedWithMaterials
   | EVertexFactoryFlags::SupportsDynamicLighting				
   | EVertexFactoryFlags::SupportsPrecisePrevWorldPos
   | EVertexFactoryFlags::SupportsPrimitiveIdStream
   | EVertexFactoryFlags::SupportsRayTracing
   | EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
   | EVertexFactoryFlags::SupportsPSOPrecaching
	)








