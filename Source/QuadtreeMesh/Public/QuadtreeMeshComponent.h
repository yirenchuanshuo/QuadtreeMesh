// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Components/MeshComponent.h"
#include "MeshQuadTree.h"
#include "QuadtreeMeshComponent.generated.h"


enum class EQuadtreeMeshRebuildFlags
{
	None = 0,
	UpdateQuadtreeMeshInfoTexture = (1 << 1),
	UpdateQuadtreeMesh = (1 << 2),
	All = (~0),
};
ENUM_CLASS_FLAGS(EQuadtreeMeshRebuildFlags);

class FQuadtreeMeshViewExtension;


UCLASS(Blueprintable, ClassGroup=(Rendering, Common), hidecategories=(Object,Activation,"Components|Activation"), ShowCategories=(Mobility), editinlinenew, meta=(BlueprintSpawnableComponent), MinimalAPI)
class UQuadtreeMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UQuadtreeMeshComponent();

	//UFUNCTION(BlueprintCallable, Category = "QuadtreeMesh")
	

public:
	
	//UObject interface
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;

	//UMeshComponent interface
	virtual int32 GetNumMaterials() const override;

	//UPrimitiveComponent interface
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	//~ Begin USceneComponent Interface.
	virtual void OnVisibilityChanged() override;
	virtual void OnHiddenInGameChanged()override;

	void UpdateComponentVisibility(bool bIsVisible);
	
#if WITH_EDITOR
	virtual bool ShouldRenderSelected() const override;
	
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//class UMaterialInterface*KnownMeshMaterial = nullptr;
#endif
	//void NotifyIfMeshMaterialChanged();
	
	void PushTessellatedQuadtreeMeshBoundsToPoxy(const FBox2D& TessellatedWaterMeshBounds)const;
	
	virtual void CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams, FComponentPSOPrecacheParamsList& OutParams) override;

	void Update();

	FVector GetDynamicQuadtreeMeshExtent()const;

	void SetExtentInTiles(FIntPoint NewExtentInTiles);
	
	FIntPoint GetExtentInTiles() const { return ExtentInTiles; }
	
	//INavRelevantInterface interface
	virtual bool IsNavigationRelevant() const override { return false; }

	const FMeshQuadTree& GetMeshQuadTree() const { return MeshQuadTree; }
	
	void MarkQuadtreeMeshGridDirty() { bNeedsRebuild = true; }

	float GetTileSize() const { return TileSize; }

	FMaterialRelevance GetWaterMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const;

	float GetLODScale() const { return LODScale; }

	int32 GetTessellationFactor() const { return FMath::Clamp(TessellationFactor, 1, 12); }

private:
	//USceneComponent interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	/** Based on all water bodies in the scene, rebuild the water mesh */
	void RebuildQuadtreeMesh(float InTileSize, const FIntPoint& InExtentInTiles);
	
	bool UpdateQuadtreeMeshInfoTexture();

	

public:
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "1", ClampMax = "12"))
	int32 TessellationFactor = 6;

	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "-1"))
	int32 ForceCollapseDensityLevel = -1;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MeshDefaultMaterial;

private:
	/** World size of the QuadtreeMesh tiles at LOD0. Multiply this with the ExtentInTiles to get the world extents of the system */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "100", AllowPrivateAcces = "true"))
	float TileSize = 2400.0f;

	/** The extent of the QuadtreeMesh in number of tiles. Maximum number of tiles for this system will be ExtentInTiles.X*2*ExtentInTiles.Y*2 */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "1", AllowPrivateAcces = "true"))
	FIntPoint ExtentInTiles = FIntPoint(64, 64);

	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "0.5"))
	float LODScale = 1.0f;
	
	FMeshQuadTree MeshQuadTree;

	TSharedPtr<FQuadtreeMeshViewExtension> QuadtreeMeshViewExtension;

	bool bNeedsRebuild = true;

	FVector2f MeshHeightExtents;
	
	float GroundZMin;
};
