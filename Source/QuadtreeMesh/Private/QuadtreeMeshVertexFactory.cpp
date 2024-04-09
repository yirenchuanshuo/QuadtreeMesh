#include "QuadtreeMeshVertexFactory.h"
#include "MeshDrawShaderBindings.h"
#include "MeshBatch.h"
#include "MeshMaterialShader.h"
#include "RenderUtils.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FQuadtreeMeshVertexFactoryParameters, "QuadtreeMeshVF");
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FQuadtreeMeshVertexFactoryRaytracingParameters, "QuadtreeMeshRaytracingVF");

template <bool bWithQuadtreeMeshSelectionSupport>
class TQuadtreeMeshVertexFactoryShaderParameters : public FVertexFactoryShaderParameters
{
	DECLARE_TYPE_LAYOUT(TQuadtreeMeshVertexFactoryShaderParameters<bWithQuadtreeMeshSelectionSupport>, NonVirtual);

public:
	using QuadtreeMeshVertexFactoryType = TQuadtreeMeshVertexFactory<bWithQuadtreeMeshSelectionSupport>;
	using QuadtreeMeshUserDataType = TQuadtreeMeshUserData<bWithQuadtreeMeshSelectionSupport>;
	using QuadtreeMeshInstanceDataBuffersType = TQuadtreeMeshInstanceDataBuffers<bWithQuadtreeMeshSelectionSupport>;

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
		QuadtreeMeshVertexFactoryType* VertexFactory =  (QuadtreeMeshVertexFactoryType*)InVertexFactory;

		const QuadtreeMeshUserDataType* QuadtreeMeshUserData = static_cast<const QuadtreeMeshUserDataType*>(BatchElement.UserData);

		const  QuadtreeMeshInstanceDataBuffersType* InstanceDataBuffers = QuadtreeMeshUserData->InstanceDataBuffers;

		const int32 InstanceOffsetValue = BatchElement.UserIndex;

		ShaderBindings.Add(Shader->GetUniformBufferParameter<FQuadtreeMeshVertexFactoryParameters>(), VertexFactory->GetQuadtreeMeshVertexFactoryUniformBuffer(QuadtreeMeshUserData->RenderGroupType));

#if RHI_RAYTRACING
		if (IsRayTracingEnabled())
		{
			ShaderBindings.Add(Shader->GetUniformBufferParameter<FQuadtreeMeshVertexFactoryRaytracingParameters>(), QuadtreeMeshUserData->QuadtreeMeshVertexFactoryRaytracingVFUniformBuffer);
		}
#endif

		if (VertexStreams.Num() > 0)
		{
			for (int32 i = 0; i < QuadtreeMeshInstanceDataBuffersType::NumBuffers; ++i)
			{
				FVertexInputStream* InstanceInputStream = VertexStreams.FindByPredicate([i](const FVertexInputStream& InStream) { return InStream.StreamIndex == i+1; });
				check(InstanceInputStream);
				
				// Bind vertex buffer
				check(InstanceDataBuffers->GetBuffer(i));
				InstanceInputStream->VertexBuffer = InstanceDataBuffers->GetBuffer(i);
			}

			if (InstanceOffsetValue > 0)
			{
				VertexFactory->OffsetInstanceStreams(InstanceOffsetValue, InputStreamType, VertexStreams);
			}
		}
	}
};


// ----------------------------------------------------------------------------------

// Always implement the basic vertex factory so that it's there for both editor and non-editor builds :
IMPLEMENT_TEMPLATE_TYPE_LAYOUT(template<>, TQuadtreeMeshVertexFactoryShaderParameters<false>);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TQuadtreeMeshVertexFactory<false>, SF_Vertex, TQuadtreeMeshVertexFactoryShaderParameters<false>);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TQuadtreeMeshVertexFactory<false>, SF_Compute, TQuadtreeMeshVertexFactoryShaderParameters<false>);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TQuadtreeMeshVertexFactory<false>, SF_RayHitGroup, TQuadtreeMeshVertexFactoryShaderParameters<false>);
#endif
IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE(template<>, TQuadtreeMeshVertexFactory<false>, "/Plugin/QuadtreeMesh/Private/QuadtreeMeshVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
	| EVertexFactoryFlags::SupportsPSOPrecaching
);

#if WITH_QUADTREEMESH_SELECTION_SUPPORT
// In editor builds, also implement the vertex factory that supports water selection:
IMPLEMENT_TEMPLATE_TYPE_LAYOUT(template<>, TQuadtreeMeshVertexFactoryShaderParameters<true>);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TQuadtreeMeshVertexFactory<true>, SF_Vertex, TQuadtreeMeshVertexFactoryShaderParameters<true>);
#if RHI_RAYTRACING
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TQuadtreeMeshVertexFactory<true>, SF_Compute, TQuadtreeMeshVertexFactoryShaderParameters<true>);
IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE(TQuadtreeMeshVertexFactory<true>, SF_RayHitGroup, TQuadtreeMeshVertexFactoryShaderParameters<true>);
#endif
IMPLEMENT_TEMPLATE_VERTEX_FACTORY_TYPE(template<>, TQuadtreeMeshVertexFactory<true>, "/Plugin/QuadtreeMesh/Private/QuadtreeMeshVertexFactory.ush",
	  EVertexFactoryFlags::UsedWithMaterials
	| EVertexFactoryFlags::SupportsDynamicLighting
	| EVertexFactoryFlags::SupportsPrecisePrevWorldPos
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
	| EVertexFactoryFlags::SupportsPSOPrecaching
);

#endif


const FVertexFactoryType* GetQuadtreeMeshVertexFactoryType(bool bWithQuadtreeMeshSelectionSupport)
{
#if WITH_QUADTREEMESH_SELECTION_SUPPORT
	if (bWithQuadtreeMeshSelectionSupport)
	{
		return &TQuadtreeMeshVertexFactory<true>::StaticType;
	}
	else
#endif
	{
		check(!bWithQuadtreeMeshSelectionSupport);
		return &TQuadtreeMeshVertexFactory<false>::StaticType;
	}
}



