// Fill out your copyright notice in the Description page of Project Settings.


#include "QuadtreeMeshSubsystem.h"

#include "EngineUtils.h"
#include "QuadtreeMeshActor.h"

#if WITH_EDITOR

bool UQuadtreeMeshSubsystem::bAllowQuadtreeMeshSubsystemOnPreviewWorld = false;

#endif

UQuadtreeMeshSubsystem::UQuadtreeMeshSubsystem()
{
}

void UQuadtreeMeshSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	check(GetWorld() != nullptr);
	
	for (AQuadtreeMeshActor* QuadtreeMeshActor : TActorRange<AQuadtreeMeshActor>(GetWorld()))
	{
		if (QuadtreeMeshActor)
		{
			QuadtreeMeshActor->Update();
		}
	}
}

TStatId UQuadtreeMeshSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UQuadtreeMeshSubsystem, STATGROUP_Tickables);
}

bool UQuadtreeMeshSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
#if WITH_EDITOR
	if (WorldType == EWorldType::EditorPreview)
	{
		return bAllowQuadtreeMeshSubsystemOnPreviewWorld;
	}
#endif 

	return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE;
}

void UQuadtreeMeshSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();
	check(World != nullptr);
}

void UQuadtreeMeshSubsystem::PostInitialize()
{
	Super::PostInitialize();
	UWorld* World = GetWorld();
	check(World);
}

void UQuadtreeMeshSubsystem::Deinitialize()
{
	UWorld* World = GetWorld();
	check(World != nullptr);
	Super::Deinitialize();
}
