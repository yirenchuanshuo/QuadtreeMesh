// Fill out your copyright notice in the Description page of Project Settings.
#include "QuadtreeMeshComponent.h"
#include "QuadtreeMeshSceneProxy.h"

// Sets default values for this component's properties
UQuadtreeMeshComponent::UQuadtreeMeshComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
	SetMobility(EComponentMobility::Static);

	struct FConstructorStatics
	{
		// Find Textures
		ConstructorHelpers::FObjectFinder<UMaterialInterface> DefautMaterial;
		FConstructorStatics()
			: DefautMaterial(TEXT("MaterialInterface'/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial'"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	MeshDefaultMaterial = ConstructorStatics.DefautMaterial.Object;
}

void UQuadtreeMeshComponent::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITORONLY_DATA
	OverrideMaterials.Init(nullptr,1);
	SetMaterial(0,MeshDefaultMaterial);
#endif
}

void UQuadtreeMeshComponent::PostInitProperties()
{
	Super::PostInitProperties();
}

int32 UQuadtreeMeshComponent::GetNumMaterials() const
{
	if(OverrideMaterials[0])
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

FPrimitiveSceneProxy* UQuadtreeMeshComponent::CreateSceneProxy()
{
	return new FQuadtreeMeshSceneProxy(this);
}

void UQuadtreeMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	for (UMaterialInterface* Mat : OverrideMaterials)
	{
		if (Mat)
		{
			OutMaterials.Add(Mat);
		}
	}
}


#if WITH_EDITOR
bool UQuadtreeMeshComponent::ShouldRenderSelected() const
{
	const bool bShouldRenderSelected = UMeshComponent::ShouldRenderSelected();
	return bShouldRenderSelected;
}



void UQuadtreeMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	//NotifyIfMeshMaterialChanged();
	if (OverrideMaterials.Num())
	{
		CleanUpOverrideMaterials();
	}
	const FName PropertyName = PropertyChangedEvent.Property->GetFName();

	// 检查是否是我们关心的 OverrideMaterials 属性
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMeshComponent, OverrideMaterials))
	{
		// 如果 OverrideMaterials 为空，那么我们就初始化一个空的材质数组
		if (OverrideMaterials.IsEmpty())
		{
			OverrideMaterials.Init(nullptr, 1);
			SetMaterial(0, MeshDefaultMaterial);
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

/*void UQuadtreeMeshComponent::NotifyIfMeshMaterialChanged()
{
#if WITH_EDITOR
	if (KnownMeshMaterial != MeshMaterial)
	{
		KnownMeshMaterial = MeshMaterial;
		FObjectCacheEventSink::NotifyUsedMaterialsChanged_Concurrent(this, TArray<UMaterialInterface*>({ MeshMaterial }));

		// Update this component streaming data.
		IStreamingManager::Get().NotifyPrimitiveUpdated(this);
	}
#endif
}*/


// Called when the game starts


