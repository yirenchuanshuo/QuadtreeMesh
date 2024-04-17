#include "QuadtreeMeshVertexFactory.h"
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

		const int32 InstanceOffsetValue = BatchElement.UserIndex;

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

			if (InstanceOffsetValue > 0)
			{
				VertexFactory->OffsetInstanceStreams(InstanceOffsetValue, InputStreamType, VertexStreams);
			}
		}
	}
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
	| EVertexFactoryFlags::SupportsCachingMeshDrawCommands
	| EVertexFactoryFlags::SupportsRayTracing
	| EVertexFactoryFlags::SupportsRayTracingDynamicGeometry
	| EVertexFactoryFlags::SupportsPrimitiveIdStream
	| EVertexFactoryFlags::SupportsPSOPrecaching
	| EVertexFactoryFlags::SupportsLumenMeshCards
	);






