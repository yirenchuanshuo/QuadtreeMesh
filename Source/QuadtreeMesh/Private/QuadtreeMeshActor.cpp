// Fill out your copyright notice in the Description page of Project Settings.


#include "QuadtreeMeshActor.h"

#include "QuadtreeMeshComponent.h"


// Sets default values
AQuadtreeMeshActor::AQuadtreeMeshActor()
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
	QuadtreeMeshComponent->MarkQuadtreeMeshGridDirty();
}

// Called every frame
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

void AQuadtreeMeshActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	//如果材质发生变化，标记网格需要重新构建
	
	FName PropertyName = PropertyChangedEvent.Property->GetFName();
	if(PropertyName == GET_MEMBER_NAME_CHECKED(AQuadtreeMeshActor,MeshMaterial))
	{
		QuadtreeMeshComponent->SetMaterial(0,MeshMaterial);
		QuadtreeMeshComponent->MarkQuadtreeMeshGridDirty();
		QuadtreeMeshComponent->MarkRenderStateDirty();
		UE_LOG(LogTemp,Warning,TEXT("Material Changed Actor"));
	}
	
}

#endif

