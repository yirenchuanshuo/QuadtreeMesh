#pragma once

#include "UObject/WeakObjectPtrTemplates.h"

namespace UE::QuadtreeMeshInfo
{
	struct FRenderingContext
	{
		UTextureRenderTarget2D* TextureRenderTarget;
		//TArray<UWaterBodyComponent*> WaterBodies;
		TArray<TWeakObjectPtr<UPrimitiveComponent>> GroundPrimitiveComponents;
		float CaptureZ;
	};
	
	/*void UpdateQuadtreeMeshInfoRendering(
		FSceneInterface* Scene,
		const FRenderingContext& Context);*/
}