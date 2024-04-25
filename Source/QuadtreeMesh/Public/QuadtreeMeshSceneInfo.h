#pragma once


#include "QuadtreeMeshComponent.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace UE::QuadtreeMeshInfo
{
	struct FRenderingContext
	{
	public:
		UQuadtreeMeshComponent* QuadtreeMeshToRender = nullptr;
		UTextureRenderTarget2D* TextureRenderTarget;
		float CaptureZ;
	};
	
	void UpdateQuadtreeMeshInfoRendering(
		FSceneInterface* Scene);
}
