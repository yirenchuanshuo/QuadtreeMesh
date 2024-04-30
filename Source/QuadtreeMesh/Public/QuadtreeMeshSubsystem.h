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

	// UWorldSubsystem implementation Begin
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

	// USubsystem implementation Begin
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void PostInitialize() override;
	virtual void Deinitialize() override;
	// USubsystem implementation End

private:
#if WITH_EDITOR
	static bool bAllowQuadtreeMeshSubsystemOnPreviewWorld;
#endif
};
