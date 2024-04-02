// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Engine/Classes/Components/MeshComponent.h"
#include "QuadtreeMeshComponent.generated.h"


UCLASS(Blueprintable, ClassGroup=(Rendering, Common), hidecategories=(Object,Activation,"Components|Activation"), ShowCategories=(Mobility), editinlinenew, meta=(BlueprintSpawnableComponent), MinimalAPI)
class UQuadtreeMeshComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UQuadtreeMeshComponent();

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
	

	//INavRelevantInterface interface
	virtual bool IsNavigationRelevant() const override { return false; }

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MeshDefaultMaterial;

protected:


private:
#if WITH_EDITOR
	
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	//class UMaterialInterface*KnownMeshMaterial = nullptr;

#endif
	//void NotifyIfMeshMaterialChanged();

public:
	UPROPERTY(EditAnywhere, Category = Rendering, meta = (ClampMin = "1", ClampMax = "12"))
	int32 TessellationFactor = 6;
	
};
