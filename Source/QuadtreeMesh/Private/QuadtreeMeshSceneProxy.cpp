#include "QuadtreeMeshSceneProxy.h"
#include "QuadtreeMeshComponent.h"
#include "Materials/Material.h"

SIZE_T FQuadtreeMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FQuadtreeMeshSceneProxy::FQuadtreeMeshSceneProxy(UQuadtreeMeshComponent* Component)
	:FPrimitiveSceneProxy(Component),
	 MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
{
	
}
