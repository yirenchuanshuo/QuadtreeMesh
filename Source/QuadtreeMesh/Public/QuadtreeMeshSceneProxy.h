#pragma once
#include "Materials/MaterialInterface.h"
#include "PrimitiveSceneProxy.h"
#include "Materials/MaterialRelevance.h"
#include "RayTracingGeometry.h"


struct FRayTracingMaterialGatheringContext;

class UQuadtreeMeshComponent;

class FQuadtreeMeshSceneProxy final:public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override;

	FQuadtreeMeshSceneProxy(UQuadtreeMeshComponent* Component);

	virtual uint32 GetMemoryFootprint() const override
	{
		return(sizeof(*this) + GetAllocatedSize());
	}

protected:
	FMaterialRelevance MaterialRelevance;
	
};
