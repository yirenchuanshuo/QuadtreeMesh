#pragma once
#include "UObject/ObjectMacros.h"


struct FQuadtreeMeshGPUWork
{
	struct FCallback
	{
		class FQuadtreeMeshSceneProxy* Proxy = nullptr;
		TFunction<void(FRDGBuilder&, bool)> Function;
	};
	TArray<FCallback> Callbacks;
};
