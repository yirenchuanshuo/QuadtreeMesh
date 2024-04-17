#pragma once

#include "QuadtreeMeshSceneInfo.h"
#include "SceneViewExtension.h"

class FQuadtreeMeshViewExtension : public FWorldSceneViewExtension
{
	public:
	FQuadtreeMeshViewExtension(const FAutoRegister& AutoReg, UWorld* InWorld);
	~FQuadtreeMeshViewExtension();

	// FSceneViewExtensionBase implementation : 
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}

	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;
	// End FSceneViewExtensionBase implementation

private:
	UE::QuadtreeMeshInfo::FRenderingContext RenderContext;
};



