// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "QuadtreeMeshComponent.h"
#include "QuadtreeMeshActor.generated.h"

enum class EQuadtreeMeshRebuildFlags
{
	None = 0,
	UpdateQuadtreeMeshInfoTexture = (1 << 1),
	UpdateQuadtreeMesh = (1 << 2),
	All = (~0),
};
ENUM_CLASS_FLAGS(EQuadtreeMeshRebuildFlags);

UCLASS()
class QUADTREEMESH_API AQuadtreeMeshActor : public AActor
{
	GENERATED_BODY()

public:
	
	AQuadtreeMeshActor();
	
	UPROPERTY()
	TObjectPtr<UQuadtreeMeshComponent> QuadtreeMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuadtreeMesh")
	TObjectPtr<UMaterialInterface> MeshMaterial;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuadtreeMesh")
	float QuadtreeMeshSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuadtreeMesh", meta = (ClampMin = "1"))
	int32 LODLayer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuadtreeMesh", meta = (ClampMin = "1"))
	int32 TessellationFactor;
	
private:
	bool bNeedInfoRebuild = false;

	UPROPERTY(Category = QuadtreeMesh, EditAnywhere, AdvancedDisplay)
	int32 OverlapPriority = 0;


protected:
	
	virtual void BeginPlay() override;

public:
	UFUNCTION()
	FBox2D GetQuadtreeMeshBound2D() const;

	int32 GetOverlapPriority() const { return OverlapPriority; }
	
	virtual void Tick(float DeltaTime) override;

	virtual void OnConstruction(const FTransform& Transform) override;
	
	virtual void PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph) override;

	void Update()const;

	void MarkForRebuild(EQuadtreeMeshRebuildFlags Flags);
#if WITH_EDITORONLY_DATA
	static void DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass);
#endif

#if WITH_EDITOR
	virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;
#endif 

	virtual void PostLoad() override;


private:
	void OnExtentChanged();

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditUndo() override;
	virtual void PostEditImport() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
