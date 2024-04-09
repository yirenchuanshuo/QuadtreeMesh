#include "QuadtreeMeshSceneProxy.h"
#include "QuadtreeMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"


DECLARE_STATS_GROUP(TEXT("Quadtree Mesh"), STATGROUP_QuadtreeMesh, STATCAT_Advanced);

DECLARE_DWORD_COUNTER_STAT(TEXT("Tiles Drawn"), STAT_QuadtreeMeshTilesDrawn, STATGROUP_QuadtreeMesh);
DECLARE_DWORD_COUNTER_STAT(TEXT("Draw Calls"), STAT_QuadtreeMeshDrawCalls, STATGROUP_QuadtreeMesh);
DECLARE_DWORD_COUNTER_STAT(TEXT("Vertices Drawn"), STAT_QuadtreeMeshVerticesDrawn, STATGROUP_QuadtreeMesh);
DECLARE_DWORD_COUNTER_STAT(TEXT("Number Drawn Materials"), STAT_QuadtreeMeshDrawnMats, STATGROUP_QuadtreeMesh);

SIZE_T FQuadtreeMeshSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

FQuadtreeMeshSceneProxy::FQuadtreeMeshSceneProxy(UQuadtreeMeshComponent* Component)
	:FPrimitiveSceneProxy(Component),
	 MaterialRelevance(Component->GetMaterialRelevance(GetScene().GetFeatureLevel()))
{
	// Cache the tiles and settings
	MeshQuadTree = Component->GetMeshQuadTree();
	// Leaf size * 0.5 equals the tightest possible LOD Scale that doesn't break the morphing. Can be scaled larger
	LODScale = MeshQuadTree.GetLeafSize() * FMath::Max(Component->GetLODScale(), 0.5f);

	// Assign the force collapse level if there is one, otherwise leave it at the default
	if (Component->ForceCollapseDensityLevel > -1)
	{
		ForceCollapseDensityLevel = Component->ForceCollapseDensityLevel;
	}

	int32 NumQuads = static_cast<int32>(FMath::Pow(2.0f, static_cast<float>(Component->GetTessellationFactor())));

	QuadtreeMeshVertexFactories.Reserve(MeshQuadTree.GetTreeDepth());
	for (uint8 i = 0; i < MeshQuadTree.GetTreeDepth(); i++)
	{
		QuadtreeMeshVertexFactories.Add(new QuadtreeMeshVertexFactoryType(GetScene().GetFeatureLevel(), NumQuads, LODScale));
		BeginInitResource(QuadtreeMeshVertexFactories.Last());

		NumQuads /= 2;
		
		if (NumQuads <= 1)
		{
			break;
		}
	}

	QuadtreeMeshVertexFactories.Shrink();
	DensityCount = QuadtreeMeshVertexFactories.Num();

	const int32 TotalLeafNodes = MeshQuadTree.GetMaxLeafCount();
	QuadtreeMeshInstanceDataBuffers = new QuadtreeMeshInstanceDataBuffersType(TotalLeafNodes);

	QuadtreeMeshUserDataBuffers = new QuadtreeMeshUserDataBuffersType(QuadtreeMeshInstanceDataBuffers);

	MeshQuadTree.BuildMaterialIndices();

#if RHI_RAYTRACING
	RayTracingQuadtreeMeshData.SetNum(DensityCount);
#endif
	
}

FQuadtreeMeshSceneProxy::~FQuadtreeMeshSceneProxy()
{
	for (QuadtreeMeshVertexFactoryType* WaterFactory : QuadtreeMeshVertexFactories)
	{
		WaterFactory->ReleaseResource();
		delete WaterFactory;
	}

	delete QuadtreeMeshInstanceDataBuffers;

	delete QuadtreeMeshUserDataBuffers;

#if RHI_RAYTRACING
	for (auto& WaterDataArray : RayTracingQuadtreeMeshData)
	{
		for (auto& WaterRayTracingItem : WaterDataArray)
		{
			WaterRayTracingItem.Geometry.ReleaseResource();
			WaterRayTracingItem.DynamicVertexBuffer.Release();
		}
	}	
#endif
}

void FQuadtreeMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
	const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(QuadtreeMesh);
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuadtreeMeshSceneProxy::GetDynamicMeshElements);
	FRHICommandListBase& RHICmdList = FRHICommandListImmediate::Get();

	// The water render groups we have to render for this batch : 
	TArray<EQuadtreeMeshRenderGroupType, TInlineAllocator<QuadtreeMeshVertexFactoryType::NumRenderGroups>> BatchRenderGroups;
	// By default, render all water tiles : 
	BatchRenderGroups.Add(EQuadtreeMeshRenderGroupType::RG_RenderQuadtreeMeshTiles);

#if WITH_QUADTREEMESH_SELECTION_SUPPORT
	bool bHasSelectedInstances = IsSelected();
	const bool bSelectionRenderEnabled = GIsEditor && ViewFamily.EngineShowFlags.Selection;

	if (bSelectionRenderEnabled && bHasSelectedInstances)
	{
		// Don't render all in one group: instead, render 2 groups : first, the selected only then, the non-selected only :
		BatchRenderGroups[0] = EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly;
		BatchRenderGroups.Add(EQuadtreeMeshRenderGroupType::RG_RenderUnselectedQuadtreeMeshTilesOnly);
	}
#endif // WITH_WATER_SELECTION_SUPPORT

	if (!HasQuadtreeData())
	{
		return;
	}

	// Set up wireframe material (if needed)
	const bool bWireframe = AllowDebugViewmodes() && ViewFamily.EngineShowFlags.Wireframe;

	FColoredMaterialRenderProxy* WireframeMaterialInstance = nullptr;
	if (bWireframe)
	{
		WireframeMaterialInstance = new FColoredMaterialRenderProxy(
			GEngine->WireframeMaterial ? GEngine->WireframeMaterial->GetRenderProxy() : nullptr,
			FColor::Cyan);

		Collector.RegisterOneFrameMaterialProxy(WireframeMaterialInstance);
	}

	const int32 NumBuckets = MeshQuadTree.GetQuadtreeMeshMaterials().Num() * DensityCount;

	TArray<FMeshQuadTree::FTraversalOutput, TInlineAllocator<4>> QuadtreeMeshInstanceDataPerView;

	bool bEncounteredISRView = false;
	int32 InstanceFactor = 1;

	// Gather visible tiles, their lod and materials for all renderable views (skip right view when stereo pair is rendered instanced)
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		const FSceneView* View = Views[ViewIndex];
		if (!bEncounteredISRView && View->IsInstancedStereoPass())
		{
			bEncounteredISRView = true;
			InstanceFactor = View->GetStereoPassInstanceFactor();
		}

		// skip gathering visible tiles from instanced right eye views
		if ((VisibilityMap & (1 << ViewIndex)) && (!bEncounteredISRView || View->IsPrimarySceneView()))
		{
			const FVector ObserverPosition = View->ViewMatrices.GetViewOrigin();
			
			FQuadtreeMeshLODParams QuadtreeMeshLODParams = GetQuadtreeMeshLODParams(ObserverPosition);

	TRACE_CPUPROFILER_EVENT_SCOPE(QuadTreeTraversalPerView);

			FMeshQuadTree::FTraversalOutput& QuadtreeMeshInstanceData = QuadtreeMeshInstanceDataPerView.Emplace_GetRef();
			QuadtreeMeshInstanceData.BucketInstanceCounts.Empty(NumBuckets);
			QuadtreeMeshInstanceData.BucketInstanceCounts.AddZeroed(NumBuckets);
			

			FMeshQuadTree::FTraversalDesc TraversalDesc;
			TraversalDesc.LowestLOD = QuadtreeMeshLODParams.LowestLOD;
			TraversalDesc.HeightMorph = QuadtreeMeshLODParams.HeightLODFactor;
			TraversalDesc.LODCount = MeshQuadTree.GetTreeDepth();
			TraversalDesc.DensityCount = DensityCount;
			TraversalDesc.ForceCollapseDensityLevel = ForceCollapseDensityLevel;
			TraversalDesc.Frustum = View->ViewFrustum;
			TraversalDesc.ObserverPosition = ObserverPosition;
			TraversalDesc.PreViewTranslation = View->ViewMatrices.GetPreViewTranslation();
			TraversalDesc.LODScale = LODScale;
			TraversalDesc.bLODMorphingEnabled = true;
			TraversalDesc.TessellatedQuadtreeMeshBounds = TessellatedQuadtreeMeshBounds;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
			//Debug
			TraversalDesc.DebugPDI = Collector.GetPDI(ViewIndex);
#endif
			MeshQuadTree.BuildQuadtreeMeshTileInstanceData(TraversalDesc, QuadtreeMeshInstanceData);

			HistoricalMaxViewInstanceCount = FMath::Max(HistoricalMaxViewInstanceCount, QuadtreeMeshInstanceData.InstanceCount);
		}
	}

	// Get number of total instances for all views
	int32 TotalInstanceCount = 0;
	for (const FMeshQuadTree::FTraversalOutput& WaterInstanceData : QuadtreeMeshInstanceDataPerView)
	{
		TotalInstanceCount += WaterInstanceData.InstanceCount;
	}

	if (TotalInstanceCount == 0)
	{
		// no instance visible, early exit
		return;
	}

	QuadtreeMeshInstanceDataBuffers->Lock(RHICmdList, TotalInstanceCount * InstanceFactor);

	int32 InstanceDataOffset = 0;

	// Go through all buckets and issue one batched draw call per LOD level per material per view
	int32 TraversalIndex = 0;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		// when rendering ISR, don't process the instanced view
		if ((VisibilityMap & (1 << ViewIndex)) && (!bEncounteredISRView || Views[ViewIndex]->IsPrimarySceneView()))
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(BucketsPerView);

			FMeshQuadTree::FTraversalOutput& WaterInstanceData = QuadtreeMeshInstanceDataPerView[TraversalIndex];
			const int32 NumWaterMaterials = MeshQuadTree.GetQuadtreeMeshMaterials().Num();
			TraversalIndex++;

			for (int32 MaterialIndex = 0; MaterialIndex < NumWaterMaterials; ++MaterialIndex)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MaterialBucket);
				bool bMaterialDrawn = false;

				for (int32 DensityIndex = 0; DensityIndex < DensityCount; ++DensityIndex)
				{
					const int32 BucketIndex = MaterialIndex * DensityCount + DensityIndex;
					const int32 InstanceCount = WaterInstanceData.BucketInstanceCounts[BucketIndex];

					if (!InstanceCount)
					{
						continue;
					}

					TRACE_CPUPROFILER_EVENT_SCOPE(DensityBucket);

					const FMaterialRenderProxy* MaterialRenderProxy = (WireframeMaterialInstance != nullptr) ? WireframeMaterialInstance : MeshQuadTree.GetQuadtreeMeshMaterials()[MaterialIndex];
					check (MaterialRenderProxy != nullptr);

					bool bUseForDepthPass = false;

					// If there's a valid material, use that to figure out the depth pass status
					if (const FMaterial* BucketMaterial = MaterialRenderProxy->GetMaterialNoFallback(GetScene().GetFeatureLevel()))
					{
						// Preemptively turn off depth rendering for this mesh batch if the material doesn't need it
						bUseForDepthPass = !BucketMaterial->GetShadingModels().HasShadingModel(MSM_SingleLayerWater) && !IsTranslucentOnlyBlendMode(*BucketMaterial);
					}

					bMaterialDrawn = true;
					for (EQuadtreeMeshRenderGroupType RenderGroup : BatchRenderGroups)
					{
						// Set up mesh batch
						FMeshBatch& Mesh = Collector.AllocateMesh();
						Mesh.bWireframe = bWireframe;
						Mesh.VertexFactory = QuadtreeMeshVertexFactories[DensityIndex];
						Mesh.MaterialRenderProxy = MaterialRenderProxy;
						Mesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
						Mesh.Type = PT_TriangleList;
						Mesh.DepthPriorityGroup = SDPG_World;
						Mesh.bCanApplyViewModeOverrides = false;
						Mesh.bUseForMaterial = true;
						Mesh.CastShadow = false;
						// Preemptively turn off depth rendering for this mesh batch if the material doesn't need it
						Mesh.bUseForDepthPass = bUseForDepthPass;
						Mesh.bUseAsOccluder = false;

#if WITH_QUADTREEMESH_SELECTION_SUPPORT
						Mesh.bUseSelectionOutline = (RenderGroup == EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly);
						Mesh.bUseWireframeSelectionColoring = (RenderGroup == EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly);
#endif // WITH_WATER_SELECTION_SUPPORT

						Mesh.Elements.SetNumZeroed(1);

						{
							TRACE_CPUPROFILER_EVENT_SCOPE_STR("Setup batch element");

							// Set up one mesh batch element
							FMeshBatchElement& BatchElement = Mesh.Elements[0];

							// Set up for instancing
							//BatchElement.bIsInstancedMesh = true;
							BatchElement.NumInstances = InstanceCount;
							BatchElement.UserData = (void*)QuadtreeMeshUserDataBuffers->GetUserData(RenderGroup);
							BatchElement.UserIndex = InstanceDataOffset * InstanceFactor;

							BatchElement.FirstIndex = 0;
							BatchElement.NumPrimitives = QuadtreeMeshVertexFactories[DensityIndex]->IndexBuffer->GetIndexCount() / 3;
							BatchElement.MinVertexIndex = 0;
							BatchElement.MaxVertexIndex = QuadtreeMeshVertexFactories[DensityIndex]->VertexBuffer->GetVertexCount() - 1;

							BatchElement.IndexBuffer = QuadtreeMeshVertexFactories[DensityIndex]->IndexBuffer;
							BatchElement.PrimitiveIdMode = PrimID_ForceZero;

							// We need the uniform buffer of this primitive because it stores the proper value for the bOutputVelocity flag.
							// The identity primitive uniform buffer simply stores false for this flag which leads to missing motion vectors.
							BatchElement.PrimitiveUniformBuffer = GetUniformBuffer(); 
						}

						{
							INC_DWORD_STAT_BY(STAT_QuadtreeMeshVerticesDrawn, QuadtreeMeshVertexFactories[DensityIndex]->VertexBuffer->GetVertexCount() * InstanceCount);
							INC_DWORD_STAT(STAT_QuadtreeMeshDrawCalls);
							INC_DWORD_STAT_BY(STAT_QuadtreeMeshTilesDrawn, InstanceCount);

							TRACE_CPUPROFILER_EVENT_SCOPE(Collector.AddMesh);

							Collector.AddMesh(ViewIndex, Mesh);
						}
					}

					// Note : we're repurposing the BucketInstanceCounts array here for storing the actual offset in the buffer. This means that effectively from this point on, BucketInstanceCounts doesn't actually 
					//  contain the number of instances anymore : 
					WaterInstanceData.BucketInstanceCounts[BucketIndex] = InstanceDataOffset;
					InstanceDataOffset += InstanceCount;
				}

				INC_DWORD_STAT_BY(STAT_QuadtreeMeshDrawnMats, static_cast<int32>(bMaterialDrawn));
			}

			const int32 NumStagingInstances = WaterInstanceData.StagingInstanceData.Num();
			for (int32 Idx = 0; Idx < NumStagingInstances; ++Idx)
			{
				const FMeshQuadTree::FStagingInstanceData& Data = WaterInstanceData.StagingInstanceData[Idx];
				const int32 WriteIndex = WaterInstanceData.BucketInstanceCounts[Data.BucketIndex]++;

				for (int32 StreamIdx = 0; StreamIdx < QuadtreeMeshInstanceDataBuffersType::NumBuffers; ++StreamIdx)
				{
					TArrayView<FVector4f> BufferMemory = QuadtreeMeshInstanceDataBuffers->GetBufferMemory(StreamIdx);
					for (int32 IdxMultipliedInstance = 0; IdxMultipliedInstance < InstanceFactor; ++IdxMultipliedInstance)
					{
						BufferMemory[WriteIndex * InstanceFactor + IdxMultipliedInstance] = Data.Data[StreamIdx];
					}
				}
			}
		}
	}

	QuadtreeMeshInstanceDataBuffers->Unlock(RHICmdList);
}



FPrimitiveViewRelevance FQuadtreeMeshSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View);
	Result.bShadowRelevance = false;
	Result.bDynamicRelevance = true;
	Result.bStaticRelevance = false;
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
	return Result;
}

FQuadtreeMeshSceneProxy::FQuadtreeMeshLODParams FQuadtreeMeshSceneProxy::GetQuadtreeMeshLODParams(
	const FVector& Position) const
{
	float QuadtreeMeshHeightForLOD = 0.0f;
	MeshQuadTree.QueryInterpolatedTileBaseHeightAtLocation(FVector2D(Position), QuadtreeMeshHeightForLOD);

	// Need to let the lowest LOD morph globally towards the next LOD. When the LOD is done morphing, simply clamp the LOD in the LOD selection to effectively promote the lowest LOD to the same LOD level as the one above
	float DistToWater = FMath::Abs(Position.Z - QuadtreeMeshHeightForLOD) / LODScale;
	DistToWater = FMath::Max(DistToWater - 2.0f, 0.0f);
	DistToWater *= 2.0f;

	// Clamp to WaterTileQuadTree.GetLODCount() - 1.0f prevents the last LOD to morph
	const float FloatLOD = FMath::Clamp(FMath::Log2(DistToWater), 0.0f, MeshQuadTree.GetTreeDepth() - 1.0f);

	FQuadtreeMeshLODParams WaterLODParams;
	WaterLODParams.HeightLODFactor = FMath::Frac(FloatLOD);
	WaterLODParams.LowestLOD = FMath::Clamp(FMath::FloorToInt(FloatLOD), 0, MeshQuadTree.GetTreeDepth() - 1);
	WaterLODParams.QuadtreeMeshHeightForLOD = QuadtreeMeshHeightForLOD;

	return WaterLODParams;
}
