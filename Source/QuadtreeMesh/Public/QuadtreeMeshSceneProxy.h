#pragma once
#include "MeshQuadTree.h"
#include "Materials/MaterialInterface.h"
#include "PrimitiveSceneProxy.h"
#include "QuadtreeMeshVertexFactory.h"
#include "Materials/MaterialRelevance.h"
#include "RayTracingGeometry.h"


struct FRayTracingMaterialGatheringContext;

class UQuadtreeMeshComponent;

class FQuadtreeMeshSceneProxy final:public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	FQuadtreeMeshSceneProxy(UQuadtreeMeshComponent* Component);
	virtual ~FQuadtreeMeshSceneProxy();

	virtual uint32 GetMemoryFootprint() const override
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

	uint32 GetAllocatedSize() const 
	{
		return(FPrimitiveSceneProxy::GetAllocatedSize() + (QuadtreeMeshVertexFactories.GetAllocatedSize() + QuadtreeMeshVertexFactories.Num() * sizeof(FQuadtreeMeshVertexFactory)) + MeshQuadTree.GetAllocatedSize());
	}

	virtual bool CanBeOccluded() const override
	{
		return !MaterialRelevance.bDisableDepthTest;
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;

#if WITH_EDITOR
	virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) override;
#endif

#if RHI_RAYTRACING
	virtual void GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances) override final;
	virtual bool HasRayTracingRepresentation() const override { return true; }
	virtual bool IsRayTracingRelevant() const override { return true; }
#endif


private:
	struct FQuadtreeMeshLODParams
	{
		int32 LowestLOD;
		float HeightLODFactor;
		float QuadtreeMeshHeightForLOD;
	};

#if RHI_RAYTRACING
	struct FRayTracingQuadtreeMeshData
	{
		FRayTracingGeometry Geometry;
		FRWBuffer DynamicVertexBuffer;
	};
	
	void SetupRayTracingInstances(FRHICommandListBase& RHICmdList, int32 NumInstances, uint32 DensityIndex);

#endif

	bool HasQuadtreeData() const 
	{
		return MeshQuadTree.GetNodeCount() != 0 && DensityCount != 0;
	}

	FQuadtreeMeshLODParams GetQuadtreeMeshLODParams(const FVector& Position) const;
	
	FMaterialRelevance MaterialRelevance;

	// One vertex factory per LOD
	TArray<FQuadtreeMeshVertexFactory*> QuadtreeMeshVertexFactories;

	/** Tiles containing water, stored in a quad tree */
	FMeshQuadTree MeshQuadTree;

	/** Unique Instance data buffer shared accross water batch draw calls */	
	FQuadtreeMeshInstanceDataBuffers* QuadtreeMeshInstanceDataBuffers;

	/** Per-"water render group" user data (the number of groups might vary depending on whether we're in the editor or not) */
	FQuadtreeMeshUserDataBuffers* QuadtreeMeshUserDataBuffers;

	FBox2D TessellatedQuadtreeMeshBounds = FBox2D(ForceInit);

	int32 ForceCollapseDensityLevel = TNumericLimits<int32>::Max();

	float LODScale = -1.0f;

	int32 DensityCount = 0;

	mutable int32 HistoricalMaxViewInstanceCount = 0;


#if RHI_RAYTRACING
	// Per density array of ray tracing geometries.
	TArray<TArray<FRayTracingQuadtreeMeshData>> RayTracingQuadtreeMeshData;	
#endif
	
};
