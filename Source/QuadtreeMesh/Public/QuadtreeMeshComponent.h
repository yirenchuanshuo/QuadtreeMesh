// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "Components/MeshComponent.h"
#include "MeshQuadTree.h"
#include "QuadtreeMeshComponent.generated.h"




class FQuadtreeMeshViewExtension;


UCLASS(Blueprintable, ClassGroup=(Rendering, Common), hidecategories=(Object,Activation,"Components|Activation"), ShowCategories=(Mobility), editinlinenew, meta=(BlueprintSpawnableComponent), MinimalAPI)
class UQuadtreeMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UQuadtreeMeshComponent();

	//UFUNCTION(BlueprintCallable, Category = "QuadtreeMesh")
	//UE_DECLARE_COMPONENT_ACTOR_INTERFACE(QuadtreeMeshComponent)

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
	
	virtual void CollectPSOPrecacheData(const FPSOPrecacheParams& BasePrecachePSOParams, FMaterialInterfacePSOPrecacheParamsList& OutParams) override;

	void Update();

	FVector GetDynamicQuadtreeMeshExtent()const;

	void SetExtentInTiles();

	void SetTileSize(float NewTileSize);

	void SetLODLayer(int32 NewLODLayer);

	void SetTessellationFactor(int32 NewFactor);

	void SetMeshMaterial(UMaterialInterface* NewMaterial);
	
	FIntPoint GetExtentInTiles() const { return ExtentInTiles; }
	
	//INavRelevantInterface interface
	virtual bool IsNavigationRelevant() const override { return false; }

	const FMeshQuadTree& GetMeshQuadTree() const { return MeshQuadTree; }
	
	void MarkQuadtreeMeshGridDirty() { bNeedsRebuild = true; }

	float GetTileSize() const { return TileSize; }

	FMaterialRelevance GetQuadtreeMeshMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const;

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

	UPROPERTY(EditAnywhere, Category = Rendering)
	TObjectPtr<UMaterialInterface> MeshMaterial;

private:
	/** World size of the QuadtreeMesh tiles at LOD0. Multiply this with the ExtentInTiles to get the world extents of the system */
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "100", AllowPrivateAcces = "true"))
	float TileSize;

	/** The extent of the QuadtreeMesh in number of tiles. Maximum number of tiles for this system will be ExtentInTiles.X*2*ExtentInTiles.Y*2 */
	UPROPERTY(Transient, VisibleAnywhere, Category = Rendering)
	FIntPoint ExtentInTiles;

	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "0.5"))
	float LODScale;

	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "0"))
	int32 LODLayer;
	
	FMeshQuadTree MeshQuadTree;

	TSharedPtr<FQuadtreeMeshViewExtension> QuadtreeMeshViewExtension;

	bool bNeedsRebuild = true;

	bool bIsInit = true;

	FVector2f MeshHeightExtents;
	
	float GroundZMin;
};
