// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "QuadtreeMeshSubsystem.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class QUADTREEMESH_API UQuadtreeMeshSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	UQuadtreeMeshSubsystem();

	// FTickableGameObject implementation Begin
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override;

private:
	
};
