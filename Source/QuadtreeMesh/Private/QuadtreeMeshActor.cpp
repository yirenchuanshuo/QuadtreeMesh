// Fill out your copyright notice in the Description page of Project Settings.


#include "QuadtreeMeshActor.h"


// Sets default values
AQuadtreeMeshActor::AQuadtreeMeshActor()
	:QuadtreeMeshExtent(FVector2D(2048.f,2048.f))
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	QuadtreeMeshComponent = CreateDefaultSubobject<UQuadtreeMeshComponent>(TEXT("QuadtreeMeshComponent"));
	
	SetRootComponent(QuadtreeMeshComponent);

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
	QuadtreeMeshComponent->SetMaterial(0,MeshMaterial);
	
	QuadtreeMeshComponent->Update();
}

// Called when the game starts or when spawned
void AQuadtreeMeshActor::BeginPlay()
{
	Super::BeginPlay();
	MarkForRebuild(EQuadtreeMeshRebuildFlags::All);
}

FBox2D AQuadtreeMeshActor::GetQuadtreeMeshBound2D() const
{
	const FVector2D QuadMeshActorLocation = FVector2D(GetActorLocation());
	const FVector2D QuadMeshActorHalfExtent = QuadtreeMeshExtent / 2.0;
	const FBox2D QuadMeshBounds(QuadMeshActorLocation - QuadMeshActorHalfExtent, QuadMeshActorLocation + QuadMeshActorHalfExtent);

	return QuadMeshBounds;
}


void AQuadtreeMeshActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void AQuadtreeMeshActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
}


void AQuadtreeMeshActor::PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph)
{
	QuadtreeMeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Super::PostLoadSubobjects(OuterInstanceGraph);
}

void AQuadtreeMeshActor::Update()const
{
	if(QuadtreeMeshComponent)
	{
		QuadtreeMeshComponent->Update();
	}
}



void AQuadtreeMeshActor::MarkForRebuild(EQuadtreeMeshRebuildFlags Flags)
{
	if (EnumHasAnyFlags(Flags, EQuadtreeMeshRebuildFlags::UpdateQuadtreeMesh))
	{
		QuadtreeMeshComponent->MarkQuadtreeMeshGridDirty();
	}
	if (EnumHasAnyFlags(Flags, EQuadtreeMeshRebuildFlags::UpdateQuadtreeMeshInfoTexture))
	{
		bNeedInfoRebuild = true;
	}
}

void AQuadtreeMeshActor::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	const FVector2D ExtentInTiles = 2.0 * FVector2D(QuadtreeMeshComponent->GetExtentInTiles());
	QuadtreeMeshExtent = FVector2D(ExtentInTiles * QuadtreeMeshComponent->GetTileSize());
	OnExtentChanged();
#endif
}

#if WITH_EDITOR

void AQuadtreeMeshActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	QuadtreeMeshComponent->MarkQuadtreeMeshGridDirty();
}

void AQuadtreeMeshActor::PostEditUndo()
{
	Super::PostEditUndo();
	QuadtreeMeshComponent->MarkQuadtreeMeshGridDirty();
}

void AQuadtreeMeshActor::PostEditImport()
{
	Super::PostEditImport();
	MarkForRebuild(EQuadtreeMeshRebuildFlags::All);
}

void AQuadtreeMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	//如果材质发生变化，标记网格需要重新构建
	
	FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if(PropertyName== GET_MEMBER_NAME_CHECKED(AQuadtreeMeshActor,QuadtreeMeshExtent))
	{
		OnExtentChanged();
	}
	if(PropertyName == GET_MEMBER_NAME_CHECKED(AQuadtreeMeshActor,MeshMaterial))
	{
		QuadtreeMeshComponent->SetMaterial(0,MeshMaterial);
		QuadtreeMeshComponent->MarkQuadtreeMeshGridDirty();
		QuadtreeMeshComponent->MarkRenderStateDirty();
	}
	
}

void AQuadtreeMeshActor::OnExtentChanged()const 
{
	// Compute the new tile extent based on the new bounds
	const float MeshTileSize = QuadtreeMeshComponent->GetTileSize();
	const FVector2D ZoneHalfExtent = QuadtreeMeshExtent / 2.0;

	int32 NewExtentInTilesX = FMath::FloorToInt(ZoneHalfExtent.X / MeshTileSize);
	int32 NewExtentInTilesY = FMath::FloorToInt(ZoneHalfExtent.Y / MeshTileSize);
	
	// We must ensure that the zone is always at least 1x1
	NewExtentInTilesX = FMath::Max(1, NewExtentInTilesX);
	NewExtentInTilesY = FMath::Max(1, NewExtentInTilesY);

	QuadtreeMeshComponent->SetExtentInTiles(FIntPoint(NewExtentInTilesX, NewExtentInTilesY));
}

#endif

