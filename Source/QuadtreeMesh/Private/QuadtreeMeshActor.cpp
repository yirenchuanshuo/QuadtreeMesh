﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "QuadtreeMeshActor.h"

#include "QuadtreeMeshActorDesc.h"


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

#if WITH_EDITORONLY_DATA
void AQuadtreeMeshActor::DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses,
	const UClass* SpecificSubclass)
{
	Super::DeclareConstructClasses(OutConstructClasses, SpecificSubclass);
	//OutConstructClasses.Add(FTopLevelAssetPath(UBoxComponent::StaticClass()));
}
#endif

#if WITH_EDITOR
TUniquePtr<FWorldPartitionActorDesc> AQuadtreeMeshActor::CreateClassActorDesc() const
{
	return MakeUnique<FQuadtreeMeshActorDesc>();
}
#endif

void AQuadtreeMeshActor::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	OnExtentChanged();
	MarkForRebuild(EQuadtreeMeshRebuildFlags::All);
#endif
}

#if WITH_EDITOR

void AQuadtreeMeshActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	MarkForRebuild(EQuadtreeMeshRebuildFlags::All);
}

void AQuadtreeMeshActor::PostEditUndo()
{
	Super::PostEditUndo();
	MarkForRebuild(EQuadtreeMeshRebuildFlags::All);
}

void AQuadtreeMeshActor::PostEditImport()
{
	Super::PostEditImport();
	MarkForRebuild(EQuadtreeMeshRebuildFlags::All);
}

void AQuadtreeMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName PropertyName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	if(PropertyName == GET_MEMBER_NAME_CHECKED(AQuadtreeMeshActor,QuadtreeMeshExtent))
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
#endif

void AQuadtreeMeshActor::OnExtentChanged()
{
	QuadtreeMeshComponent->SetTileSize(QuadtreeMeshExtent.X);
	MarkForRebuild(EQuadtreeMeshRebuildFlags::All);
}



