﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Components/MeshComponent.h"
#include "MeshQuadTree.h"
#include "QuadtreeMeshComponent.generated.h"


UCLASS(Blueprintable, ClassGroup=(Rendering, Common), hidecategories=(Object,Activation,"Components|Activation"), ShowCategories=(Mobility), editinlinenew, meta=(BlueprintSpawnableComponent), MinimalAPI)
class UQuadtreeMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UQuadtreeMeshComponent();

	virtual ~UQuadtreeMeshComponent();

	//UObject interface
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

	//UMeshComponent interface
	virtual int32 GetNumMaterials() const override;

	//UPrimitiveComponent interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	
#if WITH_EDITOR
	virtual bool ShouldRenderSelected() const override;
#endif
	
	virtual void CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams, FComponentPSOPrecacheParamsList& OutParams) override;
	//INavRelevantInterface interface
	virtual bool IsNavigationRelevant() const override { return false; }

	const FMeshQuadTree& GetWaterQuadTree() const { return MeshQuadTree; }

	void MarkQuadMeshGridDirty() { bNeedsRebuild = true; }

	void Update();

protected:


private:
	//USceneComponent interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	/** Based on all water bodies in the scene, rebuild the water mesh */
	void RebuildQuadMesh(float InTileSize, const FIntPoint& InExtentInTiles);
	
#if WITH_EDITOR
	
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	//class UMaterialInterface*KnownMeshMaterial = nullptr;

#endif
	//void NotifyIfMeshMaterialChanged();

public:
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "1", ClampMax = "12"))
	int32 TessellationFactor = 6;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MeshDefaultMaterial;

private:
	/** World size of the quadmesh tiles at LOD0. Multiply this with the ExtentInTiles to get the world extents of the system */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "100", AllowPrivateAcces = "true"))
	float TileSize = 2400.0f;

	/** The extent of the quadmesh in number of tiles. Maximum number of tiles for this system will be ExtentInTiles.X*2*ExtentInTiles.Y*2 */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "1", AllowPrivateAcces = "true"))
	FIntPoint ExtentInTiles = FIntPoint(64, 64);
	
	FMeshQuadTree MeshQuadTree;

	bool bNeedsRebuild = true;
	
};
