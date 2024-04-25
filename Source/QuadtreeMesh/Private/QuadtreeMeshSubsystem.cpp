// Fill out your copyright notice in the Description page of Project Settings.


#include "QuadtreeMeshSubsystem.h"

#include "EngineUtils.h"
#include "QuadtreeMeshActor.h"

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
