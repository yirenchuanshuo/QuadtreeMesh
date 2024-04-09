// Fill out your copyright notice in the Description page of Project Settings.

#include "QuadtreeMeshComponent.h"
#include "QuadtreeMeshSceneProxy.h"
#include "EngineUtils.h"



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
			: DefautMaterial(TEXT("MaterialInterface'DefaultMaterialName'"))
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
	return 0;
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

void UQuadtreeMeshComponent::CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams,
	FComponentPSOPrecacheParamsList& OutParams)
{
	const FVertexFactoryType* QuadtreeMeshVertexFactoryType = nullptr;
	for (UMaterialInterface* MaterialInterface : OverrideMaterials)
	{
		if (MaterialInterface)
		{
			FComponentPSOPrecacheParams& ComponentParams = OutParams[OutParams.AddDefaulted()];
			ComponentParams.Priority = EPSOPrecachePriority::High;
			ComponentParams.MaterialInterface = MaterialInterface;
			ComponentParams.VertexFactoryDataList.Add(FPSOPrecacheVertexFactoryData(QuadtreeMeshVertexFactoryType));
			ComponentParams.PSOPrecacheParams = BasePrecachePSOParams;
		}
	}
	
}


void UQuadtreeMeshComponent::Update()
{
	if(bNeedsRebuild)
	{
		RebuildQuadtreeMesh(TileSize,ExtentInTiles);
		PrecachePSOs();
		bNeedsRebuild = false;
	}
}

FMaterialRelevance UQuadtreeMeshComponent::GetWaterMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
{
	FMaterialRelevance Result;
	for (UMaterialInterface* Mat : OverrideMaterials)
	{
		Result |= Mat->GetRelevance_Concurrent(InFeatureLevel);
	}
	return Result;
}

FBoxSphereBounds UQuadtreeMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Always return valid bounds (tree is initialized with invalid bounds and if nothing is inserted, the tree bounds will stay invalid)
	FBox NewBounds = MeshQuadTree.GetBounds();

	if (NewBounds.Min.Z >= NewBounds.Max.Z)
	{
		NewBounds.Min.Z = 0.0f;
		NewBounds.Max.Z = 100.0f;
	}
	
	return NewBounds;
}

void UQuadtreeMeshComponent::RebuildQuadtreeMesh(float InTileSize, const FIntPoint& InExtentInTiles)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RebuildQuadtreeMesh);
	// Position snapped to the grid
	const FVector2D GridPosition = FVector2D(FMath::GridSnap<FVector::FReal>(GetComponentLocation().X, InTileSize), FMath::GridSnap<FVector::FReal>(GetComponentLocation().Y, InTileSize));
	const FVector2D WorldExtent = FVector2D(InTileSize * InExtentInTiles.X, InTileSize * InExtentInTiles.Y);

	const FBox2D MeshWorldBox = FBox2D(-WorldExtent + GridPosition, WorldExtent + GridPosition);
	MeshQuadTree.InitTree(MeshWorldBox,InTileSize, InExtentInTiles);

	const float QuadtreeMeshHeight = this->GetComponentLocation().Z;
	FQuadtreeMeshRenderData RenderData;
	if(!ShouldRender())
	{
		return;
	}
	RenderData.Material = OverrideMaterials[0];
	RenderData.SurfaceBaseHeight = this->GetComponentLocation().Z;
	
	AActor* QuadtreeMeshOwner = GetOwner();
#if WITH_QUADTREEMESH_SELECTION_SUPPORT
	RenderData.HitProxy = new HActor(/*InActor = */QuadtreeMeshOwner, /*InPrimComponent = */nullptr);
	RenderData.bQuadtreeMeshSelected = QuadtreeMeshOwner->IsSelected();
#endif

	const uint32 QuadtreeMeshRenderDataIndex = MeshQuadTree.AddQuadtreeMeshRenderData(RenderData);
	const FBox OceanBounds = QuadtreeMeshOwner->GetComponentsBoundingBox();
	MeshQuadTree.AddQuadtreeMeshTilesInsideBounds(OceanBounds, QuadtreeMeshRenderDataIndex);
	MeshQuadTree.Unlock(true);
	MarkRenderStateDirty();
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
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMeshComponent, OverrideMaterials)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, ForceCollapseDensityLevel))
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


