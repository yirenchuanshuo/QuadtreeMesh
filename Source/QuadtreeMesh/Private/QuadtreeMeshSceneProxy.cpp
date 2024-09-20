#include "QuadtreeMeshSceneProxy.h"
#include "QuadtreeMeshComponent.h"
#include "QuadtreeMeshViewExtension.h"
#include "RayTracingInstance.h"
#include "RenderGraphBuilder.h"
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
	 MaterialRelevance(Component->GetQuadtreeMeshMaterialRelevance(GetScene().GetFeatureLevel())),
	 bIsVisble(Component->IsVisible())
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
		QuadtreeMeshVertexFactories.Add(new FQuadtreeMeshVertexFactory(GetScene().GetFeatureLevel(), NumQuads, LODScale));
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
	QuadtreeMeshInstanceDataBuffers = new FQuadtreeMeshInstanceDataBuffers(TotalLeafNodes);

	QuadtreeMeshUserDataBuffers = new FQuadtreeMeshUserDataBuffers(QuadtreeMeshInstanceDataBuffers);

	MeshQuadTree.BuildMaterialIndices();
	
	
#if RHI_RAYTRACING
	RayTracingQuadtreeMeshData.SetNum(DensityCount);
#endif
	
}

FQuadtreeMeshSceneProxy::~FQuadtreeMeshSceneProxy()
{
	for (FQuadtreeMeshVertexFactory* QuadtreeMeshFactory : QuadtreeMeshVertexFactories)
	{
		QuadtreeMeshFactory->ReleaseResource();
		delete QuadtreeMeshFactory;
	}

	delete QuadtreeMeshInstanceDataBuffers;

	delete QuadtreeMeshUserDataBuffers;

#if RHI_RAYTRACING
	for (auto& QuadtreeMeshDataArray : RayTracingQuadtreeMeshData)
	{
		for (auto& QuadtreeMeshRayTracingItem : QuadtreeMeshDataArray)
		{
			QuadtreeMeshRayTracingItem.Geometry.ReleaseResource();
			QuadtreeMeshRayTracingItem.DynamicVertexBuffer.Release();
		}
	}	
#endif
}

void FQuadtreeMeshSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	SceneProxyCreatedFrameNumberRenderThread = GFrameNumberRenderThread;

	if (MeshQuadTree.IsGPUQuadTree())
	{
		FQuadtreeMeshGPUWork::FCallback Callback;
		Callback.Proxy = this;
		Callback.Function = [this](FRDGBuilder& GraphBuilder, bool bDepthBufferIsPopulated)
		{
			/*if (bNeedToTraverseGPUQuadTree)
			{
				if (!QuadTreeGPU.IsInitialized())
				{
					BuildGPUQuadTree(GraphBuilder);
				}
				FWaterQuadTreeGPU::FTraverseParams TraverseParams = MoveTemp(WaterQuadTreeGPUTraverseParams);
				TraverseParams.bDepthBufferIsPopulated = bDepthBufferIsPopulated;
				QuadTreeGPU.Traverse(GraphBuilder, TraverseParams);
				bNeedToTraverseGPUQuadTree = false;
				WaterQuadTreeGPUTraverseParams = {};
			}*/
		};
		//GWaterMeshGPUWork.Callbacks.Add(MoveTemp(Callback));
	}
}

void FQuadtreeMeshSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
                                                     const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(QuadtreeMesh);
	TRACE_CPUPROFILER_EVENT_SCOPE(FQuadtreeMeshSceneProxy::GetDynamicMeshElements);

	if(!bIsVisble)
	{
		return;
	}
	
	FRHICommandListBase& RHICmdList = Collector.GetRHICommandList();

	// The water render groups we have to render for this batch : 
	TArray<EQuadtreeMeshRenderGroupType, TInlineAllocator<FQuadtreeMeshVertexFactory::NumRenderGroups>> BatchRenderGroups;
	// By default, render all water tiles : 
	BatchRenderGroups.Add(EQuadtreeMeshRenderGroupType::RG_RenderQuadtreeMeshTiles);


	bool bHasSelectedInstances = IsSelected();
	const bool bSelectionRenderEnabled = GIsEditor && ViewFamily.EngineShowFlags.Selection;

	if (bSelectionRenderEnabled && bHasSelectedInstances)
	{
		// Don't render all in one group: instead, render 2 groups : first, the selected only then, the non-selected only :
		BatchRenderGroups[0] = EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly;
		BatchRenderGroups.Add(EQuadtreeMeshRenderGroupType::RG_RenderUnselectedQuadtreeMeshTilesOnly);
	}

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
			FColor(103,224,102));

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
			TraversalDesc.DebugPDI = Collector.GetPDI(ViewIndex);
#endif
			MeshQuadTree.BuildQuadtreeMeshTileInstanceData(TraversalDesc, QuadtreeMeshInstanceData);
			
			HistoricalMaxViewInstanceCount = FMath::Max(HistoricalMaxViewInstanceCount, QuadtreeMeshInstanceData.InstanceCount);
		}
	}

	// Get number of total instances for all views
	int32 TotalInstanceCount = 0;
	for (const FMeshQuadTree::FTraversalOutput& QuadtreeMeshInstanceData : QuadtreeMeshInstanceDataPerView)
	{
		TotalInstanceCount += QuadtreeMeshInstanceData.InstanceCount;
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

			FMeshQuadTree::FTraversalOutput& QuadtreeMeshInstanceData = QuadtreeMeshInstanceDataPerView[TraversalIndex];
			const int32 NumQuadtreeMeshMaterials = MeshQuadTree.GetQuadtreeMeshMaterials().Num();
			TraversalIndex++;

			for (int32 MaterialIndex = 0; MaterialIndex < NumQuadtreeMeshMaterials; ++MaterialIndex)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(MaterialBucket);
				bool bMaterialDrawn = false;

				for (int32 DensityIndex = 0; DensityIndex < DensityCount; ++DensityIndex)
				{
					const int32 BucketIndex = MaterialIndex * DensityCount + DensityIndex;
					const int32 InstanceCount = QuadtreeMeshInstanceData.BucketInstanceCounts[BucketIndex];

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
						//Mesh.bCanApplyViewModeOverrides = true;
						Mesh.bUseForMaterial = true;
						Mesh.CastShadow = false;
						// Preemptively turn off depth rendering for this mesh batch if the material doesn't need it
						Mesh.bUseForDepthPass = bUseForDepthPass;
						Mesh.bUseAsOccluder = false;


						Mesh.bUseSelectionOutline = (RenderGroup == EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly);
						Mesh.bUseWireframeSelectionColoring = (RenderGroup == EQuadtreeMeshRenderGroupType::RG_RenderSelectedQuadtreeMeshTilesOnly);


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
					QuadtreeMeshInstanceData.BucketInstanceCounts[BucketIndex] = InstanceDataOffset;
					InstanceDataOffset += InstanceCount;
				}

				INC_DWORD_STAT_BY(STAT_QuadtreeMeshDrawnMats, static_cast<int32>(bMaterialDrawn));
			}

			const int32 NumStagingInstances = QuadtreeMeshInstanceData.StagingInstanceData.Num();
			for (int32 Idx = 0; Idx < NumStagingInstances; ++Idx)
			{
				const FMeshQuadTree::FStagingInstanceData& Data = QuadtreeMeshInstanceData.StagingInstanceData[Idx];
				const int32 WriteIndex = QuadtreeMeshInstanceData.BucketInstanceCounts[Data.BucketIndex]++;

				for (int32 StreamIdx = 0; StreamIdx < FQuadtreeMeshInstanceDataBuffers::NumBuffers; ++StreamIdx)
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
	Result.bUsesLightingChannels = true;
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	MaterialRelevance.SetPrimitiveViewRelevance(Result);
	Result.bVelocityRelevance = DrawsVelocity() && Result.bOpaque && Result.bRenderInMainPass;
	return Result;
}

void FQuadtreeMeshSceneProxy::OnTessellatedQuadtreeMeshBoundsChanged_GameThread(const FBox2D& InTessellatedWaterMeshBounds)
{
	check(IsInParallelGameThread() || IsInGameThread());

	FQuadtreeMeshSceneProxy* SceneProxy = this;
	ENQUEUE_RENDER_COMMAND(OnTessellatedQuadtreeMeshBoundsChanged)(
		[SceneProxy, InTessellatedWaterMeshBounds](FRHICommandListImmediate& RHICmdList)
		{
			SceneProxy->OnTessellatedQuadtreeMeshBoundsChanged_RenderThread(InTessellatedWaterMeshBounds);
		});
}



void FQuadtreeMeshSceneProxy::OnTessellatedQuadtreeMeshBoundsChanged_RenderThread(
	const FBox2D& InTessellatedWaterMeshBounds)
{
	check(IsInRenderingThread());

	TessellatedQuadtreeMeshBounds = InTessellatedWaterMeshBounds;
}

HHitProxy* FQuadtreeMeshSceneProxy::CreateHitProxies(UPrimitiveComponent* Component,
                                                     TArray<TRefCountPtr<HHitProxy>>& OutHitProxies)
{
	MeshQuadTree.GatherHitProxies(OutHitProxies);
	return nullptr;
}


class FQuadtreeMeshVertexFactoryUserDataWrapper : public FOneFrameResource
{
public:
	FQuadtreeMeshUserData UserData;
};

void FQuadtreeMeshSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context,
	TArray<FRayTracingInstance>& OutRayTracingInstances)
{
	if (!HasQuadtreeData())
	{
		return;
	}

	const FSceneView& SceneView = *Context.ReferenceView;
	const FVector ObserverPosition = SceneView.ViewMatrices.GetViewOrigin();

	FQuadtreeMeshLODParams QuadtreeMeshLODParams = GetQuadtreeMeshLODParams(ObserverPosition);

	const int32 NumBuckets = MeshQuadTree.GetQuadtreeMeshMaterials().Num() * DensityCount;

	FMeshQuadTree::FTraversalOutput QuadtreeMeshInstanceData;
	QuadtreeMeshInstanceData.BucketInstanceCounts.Empty(NumBuckets);
	QuadtreeMeshInstanceData.BucketInstanceCounts.AddZeroed(NumBuckets);

	FMeshQuadTree::FTraversalDesc TraversalDesc;
	TraversalDesc.LowestLOD = QuadtreeMeshLODParams.LowestLOD;
	TraversalDesc.HeightMorph = QuadtreeMeshLODParams.HeightLODFactor;
	TraversalDesc.LODCount = MeshQuadTree.GetTreeDepth();
	TraversalDesc.DensityCount = DensityCount;
	TraversalDesc.ForceCollapseDensityLevel = ForceCollapseDensityLevel;
	TraversalDesc.PreViewTranslation = SceneView.ViewMatrices.GetPreViewTranslation();
	TraversalDesc.ObserverPosition = ObserverPosition;
	TraversalDesc.Frustum = FConvexVolume(); // Default volume to disable frustum culling
	TraversalDesc.LODScale = LODScale;
	TraversalDesc.bLODMorphingEnabled = true;
	TraversalDesc.TessellatedQuadtreeMeshBounds = TessellatedQuadtreeMeshBounds;

	MeshQuadTree.BuildQuadtreeMeshTileInstanceData(TraversalDesc, QuadtreeMeshInstanceData);

	if (QuadtreeMeshInstanceData.InstanceCount == 0)
	{
		// no instance visible, early exit
		return;
	}

	const int32 NumQuadtreeMeshMaterials = MeshQuadTree.GetQuadtreeMeshMaterials().Num();	

	for (int32 DensityIndex = 0; DensityIndex < DensityCount; ++DensityIndex)
	{
		int32 DensityInstanceCount = 0;
		for (int32 MaterialIndex = 0; MaterialIndex < NumQuadtreeMeshMaterials; ++MaterialIndex)
		{
			const int32 BucketIndex = MaterialIndex * DensityCount + DensityIndex;
			const int32 InstanceCount = QuadtreeMeshInstanceData.BucketInstanceCounts[BucketIndex];
			DensityInstanceCount += InstanceCount;
		}

		SetupRayTracingInstances(Context.GraphBuilder.RHICmdList, DensityInstanceCount, DensityIndex);
	}

	// Create per-bucket prefix sum and sort instance data so we can easily access per-instance data for each density
	TArray<int32> BucketOffsets;
	BucketOffsets.SetNumZeroed(NumBuckets);

	for (int32 BucketIndex = 1; BucketIndex < NumBuckets; ++BucketIndex)
	{
		BucketOffsets[BucketIndex] = BucketOffsets[BucketIndex - 1] + QuadtreeMeshInstanceData.BucketInstanceCounts[BucketIndex - 1];
	}
	
	QuadtreeMeshInstanceData.StagingInstanceData.StableSort([](const FMeshQuadTree::FStagingInstanceData& Lhs, const FMeshQuadTree::FStagingInstanceData& Rhs)
		{
			return Lhs.BucketIndex < Rhs.BucketIndex;
		});

	FMeshBatch BaseMesh;
	BaseMesh.ReverseCulling = IsLocalToWorldDeterminantNegative();
	BaseMesh.Type = PT_TriangleList;
	BaseMesh.bUseForMaterial = true;
	BaseMesh.CastShadow = false;
	BaseMesh.CastRayTracedShadow = false;
	BaseMesh.SegmentIndex = 0;
	BaseMesh.Elements.AddZeroed();

	for (int32 DensityIndex = 0; DensityIndex < DensityCount; ++DensityIndex)
	{
		int32 DensityInstanceIndex = 0;
		
		BaseMesh.VertexFactory = QuadtreeMeshVertexFactories[DensityIndex];

		FMeshBatchElement& BatchElement = BaseMesh.Elements[0];

		BatchElement.NumInstances = 1;

		BatchElement.FirstIndex = 0;
		BatchElement.NumPrimitives = QuadtreeMeshVertexFactories[DensityIndex]->IndexBuffer->GetIndexCount() / 3;
		BatchElement.MinVertexIndex = 0;
		BatchElement.MaxVertexIndex = QuadtreeMeshVertexFactories[DensityIndex]->VertexBuffer->GetVertexCount() - 1;

		// Don't use primitive buffer
		BatchElement.IndexBuffer = QuadtreeMeshVertexFactories[DensityIndex]->IndexBuffer;
		BatchElement.PrimitiveIdMode = PrimID_ForceZero;
		BatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;

		for (int32 MaterialIndex = 0; MaterialIndex < NumQuadtreeMeshMaterials; ++MaterialIndex)
		{
			const int32 BucketIndex = MaterialIndex * DensityCount + DensityIndex;
			const int32 InstanceCount = QuadtreeMeshInstanceData.BucketInstanceCounts[BucketIndex];

			if (!InstanceCount)
			{
				continue;
			}

			const FMaterialRenderProxy* MaterialRenderProxy = MeshQuadTree.GetQuadtreeMeshMaterials()[MaterialIndex];
			check(MaterialRenderProxy != nullptr);

			BaseMesh.MaterialRenderProxy = MaterialRenderProxy;

			for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
			{
				using FQuadtreeMeshVertexFactoryUserDataWrapperType = FQuadtreeMeshVertexFactoryUserDataWrapper;
				FQuadtreeMeshVertexFactoryUserDataWrapperType& UserDataWrapper = Context.RayTracingMeshResourceCollector.AllocateOneFrameResource<FQuadtreeMeshVertexFactoryUserDataWrapperType>();

				const int32 InstanceDataIndex = BucketOffsets[BucketIndex] + InstanceIndex;
				const FMeshQuadTree::FStagingInstanceData& InstanceData = QuadtreeMeshInstanceData.StagingInstanceData[InstanceDataIndex];

				FQuadtreeMeshVertexFactoryRaytracingParameters UniformBufferParams;
				UniformBufferParams.VertexBuffer = QuadtreeMeshVertexFactories[DensityIndex]->VertexBuffer->GetSRV();
				UniformBufferParams.InstanceData0 = InstanceData.Data[0];
				UniformBufferParams.InstanceData1 = InstanceData.Data[1];

				UserDataWrapper.UserData.InstanceDataBuffers = QuadtreeMeshUserDataBuffers->GetUserData(EQuadtreeMeshRenderGroupType::RG_RenderQuadtreeMeshTiles)->InstanceDataBuffers;
				UserDataWrapper.UserData.RenderGroupType = EQuadtreeMeshRenderGroupType::RG_RenderQuadtreeMeshTiles;
				UserDataWrapper.UserData.QuadtreeMeshVertexFactoryRaytracingVFUniformBuffer = FQuadtreeMeshVertexFactoryRaytracingParametersRef::CreateUniformBufferImmediate(UniformBufferParams, UniformBuffer_SingleFrame);
							
				BatchElement.UserData = (void*)&UserDataWrapper.UserData;							

				FRayTracingQuadtreeMeshData& QuadtreeMeshInstanceRayTracingData = RayTracingQuadtreeMeshData[DensityIndex][DensityInstanceIndex++];

				FRayTracingInstance RayTracingInstance;
				RayTracingInstance.Geometry = &QuadtreeMeshInstanceRayTracingData.Geometry;
				RayTracingInstance.InstanceTransforms.Add(GetLocalToWorld());
				RayTracingInstance.Materials.Add(BaseMesh);
				OutRayTracingInstances.Add(RayTracingInstance);

				Context.DynamicRayTracingGeometriesToUpdate.Add(
					FRayTracingDynamicGeometryUpdateParams
					{
						RayTracingInstance.Materials,
						false,
						static_cast<uint32>(QuadtreeMeshVertexFactories[DensityIndex]->VertexBuffer->GetVertexCount()),
						static_cast<uint32>(QuadtreeMeshVertexFactories[DensityIndex]->VertexBuffer->GetVertexCount() * sizeof(FVector3f)),
						static_cast<uint32>(QuadtreeMeshVertexFactories[DensityIndex]->IndexBuffer->GetIndexCount() / 3),
						&QuadtreeMeshInstanceRayTracingData.Geometry,
						nullptr,
						true
					}
				);				
			}
		}
	}
}

void FQuadtreeMeshSceneProxy::SetupRayTracingInstances(FRHICommandListBase& RHICmdList, int32 NumInstances,
	uint32 DensityIndex)
{
	TArray<FRayTracingQuadtreeMeshData>& QuadtreeMeshDataArray = RayTracingQuadtreeMeshData[DensityIndex];

	if (QuadtreeMeshDataArray.Num() > NumInstances)
	{
		for (int32 Item = NumInstances; Item < QuadtreeMeshDataArray.Num(); Item++)
		{
			auto& QuadtreeMeshItem = QuadtreeMeshDataArray[Item];
			QuadtreeMeshItem.Geometry.ReleaseResource();
			QuadtreeMeshItem.DynamicVertexBuffer.Release();
		}
		QuadtreeMeshDataArray.SetNum(NumInstances);
	}	

	if (QuadtreeMeshDataArray.Num() < NumInstances)
	{
		FRayTracingGeometryInitializer Initializer;
		static const FName DebugName("FQuadtreeMeshSceneProxy");		
		Initializer.IndexBuffer = QuadtreeMeshVertexFactories[DensityIndex]->IndexBuffer->IndexBufferRHI;
		Initializer.GeometryType = RTGT_Triangles;
		Initializer.bFastBuild = true;
		Initializer.bAllowUpdate = true;
		Initializer.TotalPrimitiveCount = 0;

		QuadtreeMeshDataArray.Reserve(NumInstances);
		const int32 StartIndex = QuadtreeMeshDataArray.Num();

		for (int32 Item = StartIndex; Item < NumInstances; Item++)
		{
			FRayTracingQuadtreeMeshData& QuadtreeMeshData = QuadtreeMeshDataArray.AddDefaulted_GetRef();

			Initializer.DebugName = FName(DebugName, Item);

			QuadtreeMeshData.Geometry.SetInitializer(Initializer);
			QuadtreeMeshData.Geometry.InitResource(RHICmdList);
		}
	}
}



FQuadtreeMeshSceneProxy::FQuadtreeMeshLODParams FQuadtreeMeshSceneProxy::GetQuadtreeMeshLODParams(
	const FVector& Position) const
{
	float QuadtreeMeshHeightForLOD = 0.0f;
	MeshQuadTree.QueryInterpolatedTileBaseHeightAtLocation(FVector2D(Position), QuadtreeMeshHeightForLOD);

	// Need to let the lowest LOD morph globally towards the next LOD. When the LOD is done morphing, simply clamp the LOD in the LOD selection to effectively promote the lowest LOD to the same LOD level as the one above
	float DistToQuadtreeMesh = FMath::Abs(Position.Z - QuadtreeMeshHeightForLOD) / LODScale;
	DistToQuadtreeMesh = FMath::Max(DistToQuadtreeMesh - 2.0f, 0.0f);
	DistToQuadtreeMesh *= 2.0f;

	// Clamp to WaterTileQuadTree.GetLODCount() - 1.0f prevents the last LOD to morph
	const float FloatLOD = FMath::Clamp(FMath::Log2(DistToQuadtreeMesh), 0.0f, MeshQuadTree.GetTreeDepth() - 1.0f);

	FQuadtreeMeshLODParams QuadtreeMeshLODParams;
	QuadtreeMeshLODParams.HeightLODFactor = FMath::Frac(FloatLOD);
	QuadtreeMeshLODParams.LowestLOD = FMath::Clamp(FMath::FloorToInt(FloatLOD), 0, MeshQuadTree.GetTreeDepth() - 1);
	QuadtreeMeshLODParams.QuadtreeMeshHeightForLOD = QuadtreeMeshHeightForLOD;

	return QuadtreeMeshLODParams;
}
