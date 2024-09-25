// Fill out your copyright notice in the Description page of Project Settings.

#include "QuadtreeMeshComponent.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "QuadtreeMeshSceneProxy.h"
#include "EngineUtils.h"
#include "MaterialDomain.h"
#include "PSOPrecacheMaterial.h"
#include "QuadtreeMeshActor.h"
#include "Chaos/ImplicitObjectBVH.h"



// Sets default values for this component's properties
UQuadtreeMeshComponent::UQuadtreeMeshComponent()
{
	bAutoActivate = true;
	bHasPerInstanceHitProxies = true;
	
	SetMobility(EComponentMobility::Static);
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial;
		FConstructorStatics():
			DefaultMaterial(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;
	MeshMaterial = ConstructorStatics.DefaultMaterial.Object;

	TileSize = 4096.f;
	ExtentInTiles = FIntPoint(64,64);
	LODScale = 1.0f;
	LODLayer = 4;
}



void UQuadtreeMeshComponent::PostInitProperties()
{
	Super::PostInitProperties();
	SetMaterial(0,MeshMaterial);
	UpdateBounds();
	MarkRenderTransformDirty();
	Update();
}

int32 UQuadtreeMeshComponent::GetNumMaterials() const
{
	return 1;
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
}

void UQuadtreeMeshComponent::OnHiddenInGameChanged()
{
	Super::OnHiddenInGameChanged();
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
	FMaterialInterfacePSOPrecacheParamsList& OutParams)
{
	FVertexFactoryType* FQuadtreeMeshVertexFactory = &FQuadtreeMeshVertexFactory::StaticType;
	for (UMaterialInterface* MaterialInterface : OverrideMaterials)
	{
		if (MaterialInterface)
		{
			FMaterialInterfacePSOPrecacheParams& ComponentParams = OutParams[OutParams.AddDefaulted()];
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

void UQuadtreeMeshComponent::SetExtentInTiles()
{
	const FVector2D QuadtreeMeshExtent = FVector2D(TileSize);
	const float QuadtreeMeshTileSize = TileSize/FMath::Max(2.0,FMath::Pow(2,static_cast<float>(LODLayer)));

	int32 NewExtentInTilesX = FMath::FloorToInt(QuadtreeMeshExtent.X / QuadtreeMeshTileSize);
	int32 NewExtentInTilesY = FMath::FloorToInt(QuadtreeMeshExtent.Y / QuadtreeMeshTileSize);
	
	
	NewExtentInTilesX = FMath::Max(1, NewExtentInTilesX);
	NewExtentInTilesY = FMath::Max(1, NewExtentInTilesY);
	
	ExtentInTiles = FIntPoint(NewExtentInTilesX, NewExtentInTilesY);
}

void UQuadtreeMeshComponent::SetTileSize(float NewTileSize)
{
	TileSize = NewTileSize;
}

void UQuadtreeMeshComponent::SetLODLayer(int32 NewLODLayer)
{
	LODLayer = NewLODLayer;
}

void UQuadtreeMeshComponent::SetTessellationFactor(int32 NewFactor)
{
	TessellationFactor = FMath::Clamp(NewFactor,1,12);
}


void UQuadtreeMeshComponent::SetMeshMaterial(UMaterialInterface* NewMaterial)
{
	MeshMaterial = NewMaterial;
	SetMaterial(0,MeshMaterial);
}

FMaterialRelevance UQuadtreeMeshComponent::GetQuadtreeMeshMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
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
	FVector Scale = GetComponentScale();
	// Position snapped to the grid
	//FVector2D GridPosition = FVector2D(FMath::GridSnap<FVector::FReal>(GetComponentLocation().X, InTileSize), FMath::GridSnap<FVector::FReal>(GetComponentLocation().Y, InTileSize))+FVector2D(GetComponentLocation().X,GetComponentLocation().Y);
	FVector2D GridPosition = FVector2D(GetComponentLocation().X,GetComponentLocation().Y);
	
	const FVector2D WorldExtent = FVector2D(InTileSize * InExtentInTiles.X, InTileSize * InExtentInTiles.Y);

	const FBox2D MeshWorldBox = FBox2D(-WorldExtent + GridPosition, WorldExtent + GridPosition);
	MeshQuadTree.InitTree(MeshWorldBox,InTileSize, InExtentInTiles,false);

	
	FVector ComponentLocation = GetComponentLocation();
	const float QuadtreeMeshHeight = ComponentLocation.Z;
	
	FQuadtreeMeshRenderData RenderData;
	if(!ShouldRender())
	{
		return;
	}
	
	
	RenderData.Material = MeshMaterial;
	RenderData.SurfaceBaseHeight = QuadtreeMeshHeight;
	
	if(AActor* QuadtreeMeshOwner = GetOwner())
	{
		RenderData.HitProxy = new HActor(/*InActor = */QuadtreeMeshOwner, /*InPrimComponent = */nullptr);
		RenderData.bQuadtreeMeshSelected = QuadtreeMeshOwner->IsSelected();
	}
	
	
	const uint32 QuadtreeMeshRenderDataIndex = MeshQuadTree.AddQuadtreeMeshRenderData(RenderData);
	FBox Bound;
	Bound.Max = FVector(InTileSize*Scale.X+GridPosition.X,InTileSize*Scale.Y+GridPosition.Y,0.0f);
	Bound.Min = FVector(-InTileSize*Scale.X+GridPosition.X,-InTileSize*Scale.Y+GridPosition.Y,0.0f);
	
	
	const FBox MeshBounds = Bound;
	MeshQuadTree.AddQuadtreeMeshTilesInsideBounds(MeshBounds, QuadtreeMeshRenderDataIndex);
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
	/*if (OverrideMaterials.Num())
	{
		CleanUpOverrideMaterials();
	}*/
	const FName PropertyName = PropertyChangedEvent.MemberProperty->GetFName();
	
	
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, ForceCollapseDensityLevel)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, TessellationFactor)
		)
	{
		MarkQuadtreeMeshGridDirty();
		MarkRenderStateDirty();
	}

	if(PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, TileSize))
	{
		SetTileSize(TileSize);
		SetExtentInTiles();
		MarkQuadtreeMeshGridDirty();
		MarkRenderStateDirty();
	}

	if(PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, LODLayer))
	{
		SetLODLayer(LODLayer);
		SetExtentInTiles();
		MarkQuadtreeMeshGridDirty();
		MarkRenderStateDirty();
	}

	if(PropertyName == GET_MEMBER_NAME_CHECKED(UQuadtreeMeshComponent, MeshMaterial))
	{
		SetMaterial(0,MeshMaterial);
#if WITH_EDITOR
		//FObjectCacheEventSink::NotifyUsedMaterialsChanged_Concurrent(this, TArray<UMaterialInterface*>({ MeshMaterial }));

		// Update this component streaming data.
		IStreamingManager::Get().NotifyPrimitiveUpdated(this);
#endif
		MarkQuadtreeMeshGridDirty();
		MarkRenderStateDirty();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
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
#if UE_WITH_PSO_PRECACHING
	if (!FApp::CanEverRender() || !IsComponentPSOPrecachingEnabled() || !IsInGameThread())
	{
		return;
	}

	// clear the current request data
	MaterialPSOPrecacheRequestIDs.Empty();
	PSOPrecacheCompileEvent = nullptr;
	bPSOPrecacheRequestBoosted = false;

	// Collect the data from the derived classes
	FPSOPrecacheParams PSOPrecacheParams;
	SetupPrecachePSOParams(PSOPrecacheParams);
	FMaterialInterfacePSOPrecacheParamsList PSOPrecacheDataArray;
	CollectPSOPrecacheData(PSOPrecacheParams, PSOPrecacheDataArray);

	FGraphEventArray GraphEvents;
	PrecacheMaterialPSOs(PSOPrecacheDataArray, MaterialPSOPrecacheRequestIDs, GraphEvents);

	RequestRecreateRenderStateWhenPSOPrecacheFinished(GraphEvents);
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


