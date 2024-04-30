// Fill out your copyright notice in the Description page of Project Settings.

#include "QuadtreeMeshComponent.h"
#include "QuadtreeMeshSceneProxy.h"
#include "EngineUtils.h"
#include "QuadtreeMeshActor.h"
#include "QuadtreeMeshRender.h"
#include "Engine/TextureRenderTarget2D.h"


// Sets default values for this component's properties
UQuadtreeMeshComponent::UQuadtreeMeshComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// ...DefaultMaterialName
	SetMobility(EComponentMobility::Static);
	
}



void UQuadtreeMeshComponent::PostInitProperties()
{
	Super::PostInitProperties();
	UpdateBounds();
	MarkRenderTransformDirty();
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
	if(RHISupportsManualVertexFetch(GMaxRHIShaderPlatform))
	{
		SceneProxy = new FQuadtreeMeshSceneProxy(this);
		return SceneProxy;
	}
	return nullptr;
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

void UQuadtreeMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
}

void UQuadtreeMeshComponent::OnVisibilityChanged()
{
	Super::OnVisibilityChanged();
	//UpdateComponentVisibility(true);
}

void UQuadtreeMeshComponent::OnHiddenInGameChanged()
{
	Super::OnHiddenInGameChanged();
	//UpdateComponentVisibility(true);
}

void UQuadtreeMeshComponent::UpdateComponentVisibility(bool bIsVisible)
{
	if(UWorld*World = GetWorld())
	{
		bool bLocalVisible = GetVisibleFlag();
		bool bLocalHiddenInGame = bHiddenInGame;
		MarkQuadtreeMeshGridDirty();
	}
}


#if WITH_EDITOR
bool UQuadtreeMeshComponent::ShouldRenderSelected() const
{
	const bool bShouldRenderSelected = UMeshComponent::ShouldRenderSelected();
	return bShouldRenderSelected;
}
#endif

void UQuadtreeMeshComponent::CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams,
	FComponentPSOPrecacheParamsList& OutParams)
{
	FVertexFactoryType* FQuadtreeMeshVertexFactory = &FQuadtreeMeshVertexFactory::StaticType;
	for (UMaterialInterface* MaterialInterface : OverrideMaterials)
	{
		if (MaterialInterface)
		{
			FComponentPSOPrecacheParams& ComponentParams = OutParams[OutParams.AddDefaulted()];
			ComponentParams.Priority = EPSOPrecachePriority::High;
			ComponentParams.MaterialInterface = MaterialInterface;
			ComponentParams.VertexFactoryDataList.Add(FPSOPrecacheVertexFactoryData(FQuadtreeMeshVertexFactory));
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


FVector UQuadtreeMeshComponent::GetDynamicQuadtreeMeshExtent() const
{
	return FVector(TileSize*2,TileSize*2,0.0f);
}

void UQuadtreeMeshComponent::SetExtentInTiles(FIntPoint NewExtentInTiles)
{
	ExtentInTiles = NewExtentInTiles;
	MarkQuadtreeMeshGridDirty();
	MarkRenderStateDirty();
}

void UQuadtreeMeshComponent::SetTileSize(float NewTileSize)
{
	TileSize = NewTileSize;
	MarkQuadtreeMeshGridDirty();
	MarkRenderStateDirty();
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

	
	
	const float QuadtreeMeshHeight = GetComponentLocation().Z;
	
	FQuadtreeMeshRenderData RenderData;
	if(!ShouldRender())
	{
		return;
	}
	RenderData.Material = OverrideMaterials[0];
	RenderData.SurfaceBaseHeight = QuadtreeMeshHeight;
	
	AQuadtreeMeshActor* QuadtreeMeshOwner = GetOwner<AQuadtreeMeshActor>();

	RenderData.HitProxy = new HActor(/*InActor = */QuadtreeMeshOwner, /*InPrimComponent = */nullptr);
	RenderData.bQuadtreeMeshSelected = QuadtreeMeshOwner->IsSelected();


	const uint32 QuadtreeMeshRenderDataIndex = MeshQuadTree.AddQuadtreeMeshRenderData(RenderData);
	FBox Bound;
	Bound.Max = FVector(InTileSize,InTileSize,0.0f);
	Bound.Min = FVector(-InTileSize,-InTileSize,0.0f);
	
	const FBox OceanBounds = Bound;
	MeshQuadTree.AddQuadtreeMeshTilesInsideBounds(OceanBounds, QuadtreeMeshRenderDataIndex);
	MeshQuadTree.Unlock(true);
	MarkRenderStateDirty();
}

bool UQuadtreeMeshComponent::UpdateQuadtreeMeshInfoTexture()
{
	return true;
}



#if WITH_EDITOR

void UQuadtreeMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	//NotifyIfMeshMaterialChanged();
	Super::PostEditChangeProperty(PropertyChangedEvent);
	
	if (OverrideMaterials.Num())
	{
		CleanUpOverrideMaterials();
	}
	const FName PropertyName = PropertyChangedEvent.Property->GetFName();
	
	// 检查是否是我们关心的 OverrideMaterials 属性
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMeshComponent, OverrideMaterials)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, ForceCollapseDensityLevel)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, TessellationFactor)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, TileSize)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, ExtentInTiles))
	{
		MarkQuadtreeMeshGridDirty();
		MarkRenderStateDirty();
	}
	
}

#endif

void UQuadtreeMeshComponent::PushTessellatedQuadtreeMeshBoundsToPoxy(const FBox2D& TessellatedWaterMeshBounds)const
{
	if (SceneProxy)
	{
		static_cast<FQuadtreeMeshSceneProxy*>(SceneProxy)->OnTessellatedQuadtreeMeshBoundsChanged_GameThread(TessellatedWaterMeshBounds);
	}
}


void UQuadtreeMeshComponent::PostLoad()
{
	Super::PostLoad();
	
	if (IsComponentPSOPrecachingEnabled())
	{
		FPSOPrecacheParams PrecachePSOParams;
		SetupPrecachePSOParams(PrecachePSOParams);
		if (OverrideMaterials[0])
		{
			OverrideMaterials[0]->ConditionalPostLoad();
			OverrideMaterials[0]->PrecachePSOs(&FLocalVertexFactory::StaticType, PrecachePSOParams);
		}
	}

#if WITH_EDITOR

	if(IsTemplate())
	{
		TWeakObjectPtr<UQuadtreeMeshComponent> QuadtreeMeshComponent = this;
		if (QuadtreeMeshComponent.IsValid())
		{
			QuadtreeMeshComponent->MarkPackageDirty();
		}
	}
#endif

	MarkQuadtreeMeshGridDirty();
	
}


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


