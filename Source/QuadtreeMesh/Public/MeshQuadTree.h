#pragma once


class UQuadtreeMeshComponent;
class FMaterialRenderProxy;
class UMaterialInterface;
class HHitProxy;

struct FQuadtreeMeshRenderData
{
	UMaterialInterface* Material = nullptr;
	double SurfaceBaseHeight = 0.0;
	FMatrix LocalToWorld = FMatrix::Identity;
	int16 MaterialIndex = INDEX_NONE;
	
	/** Hit proxy for this QUADTREEMESH */
	TRefCountPtr<HHitProxy> HitProxy = nullptr;
	/** Whether the water body actor is selected or not */
	bool bQuadtreeMeshSelected = false;

	bool operator==(const FQuadtreeMeshRenderData& Other) const
	{
		return	Material				== Other.Material &&
				SurfaceBaseHeight		== Other.SurfaceBaseHeight
				&& HitProxy == Other.HitProxy
				&& bQuadtreeMeshSelected == Other.bQuadtreeMeshSelected; 
	}
	
};


struct FMeshQuadTree
{
	enum { INVALID_PARENT = 0xFFFFFFF };

	static constexpr int32 NumStreams =  3 ;

	struct FStagingInstanceData
	{
		int32 BucketIndex;
		FVector4f Data[NumStreams];
	};

	struct FTraversalOutput
	{
		TArray<int32> BucketInstanceCounts;

		/**
		 *	This is the raw data that will be bound for the draw call through a buffer. Stored in buckets sorted by material and density level
		 *	Each instance contains:
		 *	[0] (xyz: translate, w: wave param index)
		 *	[1] (x: (bit 0-7)lod level, (bit 8)bShouldMorph, y: HeightMorph zw: scale)
		 *  [2] (editor only, HitProxy ID of the associated WaterBody actor)
		 */
		TArray<FStagingInstanceData> StagingInstanceData;

		/** Number of added instances */
		int32 InstanceCount = 0;
	};


	struct FTraversalDesc
	{
		int32 LowestLOD = 0;
		int32 LODCount = 0;
		int32 DensityCount = 0;
		float HeightMorph = 0.0f;
		int32 ForceCollapseDensityLevel = TNumericLimits<int32>::Max();
		float LODScale = 1.0;
		FVector ObserverPosition = FVector::ZeroVector;
		FVector PreViewTranslation = FVector::ZeroVector;
		FConvexVolume Frustum;
		bool bLODMorphingEnabled = true;
		FBox2D TessellatedQuadtreeMeshBounds = FBox2D(ForceInit);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		// Debug
		int32 DebugShowTile = 0;
		class FPrimitiveDrawInterface* DebugPDI = nullptr;
#endif
	};

	
	/** Obtain all possible hit proxies (proxies of all the water bodies) */
	void GatherHitProxies(TArray<TRefCountPtr<HHitProxy> >& OutHitProxies) const;


public:
	/** 
		 *	Initialize the tree. This will unlock the tree for node insertion using AddWaterTilesInsideBounds(...). 
		 *	Tree must be locked before traversal, see Lock(). 
		 */
	void InitTree(const FBox2D& InBounds, float InTileSize, FIntPoint InExtentInTiles);
	/** Unlock to make it read-only. This will optionally prune the node array to remove redundant nodes, nodes that can be implicitly traversed */
	void Unlock(bool bPruneRedundantNodes);
	/** Add tiles that intersect InBounds recursively from the root node. Tree must be unlocked. Typically called on Game Thread */
	void AddQuadtreeMeshTilesInsideBounds(const FBox& InBounds, uint32 InQuadtreeMeshIndex);
	
	void AddQuadtreeMesh(const TArray<FVector2D>& InPoly, const FBox& InMeshBounds, uint32 InQuadtreeMeshIndex);
	/** Assign an index to each material */
	void BuildMaterialIndices();

	void BuildQuadtreeMeshTileInstanceData(const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const;
	
	/** Bilinear interpolation between four neighboring base height samples around InWorldLocationXY. The samples are done on the leaf node grid resolution. Returns true if all 4 samples were taken in valid nodes */
	bool QueryInterpolatedTileBaseHeightAtLocation(const FVector2D& InWorldLocationXY, float& OutHeight) const;
	
	/** Walks down the tree and returns the tile height at InWorldLocationXY in OutWorldHeight. Returns true if the query hits an exact solution (either leaf tile or a complete subtree parent), otherwise false. */
	bool QueryTileBaseHeightAtLocation(const FVector2D& InWorldLocationXY, float& OutWorldHeight) const;
	
	/** Walks down the tree and returns the tile bounds at InWorldLocationXY in OutWorldBounds. Returns true if the query finds a leaf tile to return, otherwise false. */
	bool QueryTileBoundsAtLocation(const FVector2D& InWorldLocationXY, FBox& OutWorldBounds) const;

	/** Add water body render data to this tree. Returns the index in the array. Use this index to add tiles with this water body to the tree, see AddWaterTilesInsideBounds(..) */
	uint32 AddQuadtreeMeshRenderData(const FQuadtreeMeshRenderData& InQuadtreeMeshRenderData) { return NodeData.QuadtreeMeshRenderData.Add(InQuadtreeMeshRenderData); }

	/** Get bounds of the root node if there is one, otherwise some default box */
	FBox GetBounds() const { return NodeData.Nodes.Num() > 0 ? NodeData.Nodes[0].Bounds : FBox(-FVector::OneVector, FVector::OneVector); }
	
	/** Return the 2D region containing water tiles. Tiles can not be generated outside of this region */
	FBox2D GetTileRegion() const { return TileRegion; }
	
	/** Total node count in the tree, including inner nodes, root node and leaf nodes */
	int32 GetNodeCount() const { return NodeData.Nodes.Num(); }

	/** Get cached leaf world size of one side of the tile (same applies for X and Y) */
	float GetLeafSize() const { return LeafSize; }

	/** Number of maximum leaf nodes on one side, same applies for X and Y. (Maximum number of total leaf nodes in this tree is LeafSideCount*LeafSideCount) */
	int32 GetMaxLeafCount() const { return MaxLeafCount; }

	/** Max depth of the tree */
	int32 GetTreeDepth() const { return TreeDepth; }

	const TArray<FMaterialRenderProxy*>& GetQuadtreeMeshMaterials() const { return QuadtreeMeshMaterials; }
	/** Calculate the world distance to a LOD */
	static float GetLODDistance(int32 InLODLevel, float InLODScale) { return FMath::Pow(2.0f, static_cast<float>(InLODLevel + 1)) * InLODScale; }

	uint32 GetAllocatedSize() const { return NodeData.GetAllocatedSize() + QuadtreeMeshMaterials.GetAllocatedSize(); }

private:
	
	
	
	int32 TreeDepth = 0;

	float LeafSize = 0.0f;
	int32 MaxLeafCount = 0;
	FIntPoint ExtentInTiles = FIntPoint::ZeroValue;
	FBox2D TileRegion;
	TArray<FMaterialRenderProxy*> QuadtreeMeshMaterials;

	bool bIsReadOnly = true;

	
	struct FNodeData;

	struct FNode
	{

		FNode() : QuadtreeMeshIndex(0), TransitionQuadtreeMeshIndex(0), ParentIndex(INVALID_PARENT), HasCompleteSubtree(1), IsSubtreeSameQuadtreeMesh(1), HasMaterial(0) {}

		/** If this node is allowed to be rendered, it means it can be rendered in place of all leaf nodes in its subtree. */
		bool CanRender(int32 InDensityLevel, int32 InForceCollapseDensityLevel, const FQuadtreeMeshRenderData& InQuadtreeMeshRenderData) const;

		/** Add instance for rendering this node*/
		void AddNodeForRender(const FNodeData& InNodeData, const FQuadtreeMeshRenderData& InQuadtreeMeshRenderData, int32 InDensityLevel, int32 InLODLevel, const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const;

		/** Recursive function to traverse down to the appropriate density level. The LODLevel is constant here since this function is only called on tiles that are fully inside a LOD range */
		void SelectLODRefinement(const FNodeData& InNodeData, int32 InDensityLevel, int32 InLODLevel, const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const;

		/** Recursive function to select nodes visible from the current point of view */
		void SelectLOD(const FNodeData& InNodeData, int32 InLODLevel, const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const;

		/** Recursive function to select nodes visible from the current point of view within an active bounding box */
		void SelectLODWithinBounds(const FNodeData& InNodeData, int32 InLODLevel, const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const;

		/** Recursive function to query the height(prior to any displacement) at a given location, return false if no height could be found */
		bool QueryBaseHeightAtLocation(const FNodeData& InNodeData, const FVector2D& InWorldLocationXY, float& OutHeight) const;

		/** Recursive function to query the bounds of a tile at a given location, return false if no leaf node could be found */
		bool QueryBoundsAtLocation(const FNodeData& InNodeData, const FVector2D& InWorldLocationXY, FBox& OutBounds) const;

		/** Add nodes that intersect InMeshBounds. LODLevel is the current level. This is the only method used to generate the tree */
		void AddNodes(FNodeData& InNodeData, const FBox& InMeshBounds, const FBox& InQuadtreeMeshBounds, uint32 InQuadtreeMeshIndex, int32 InLODLevel, uint32 InParentIndex);
		
		/** Check if all conditions are met to potentially allow this and another node to render as one */
		bool CanMerge(const FNode& Other) const { return Other.QuadtreeMeshIndex == QuadtreeMeshIndex && Other.TransitionQuadtreeMeshIndex == TransitionQuadtreeMeshIndex; }

		/** World bounds */
		FBox Bounds = FBox(-FVector::OneVector, FVector::OneVector);

		/** Index into the water body render data array on the tree. If this is not a leaf node, this will represent the waterbody */
		uint32 QuadtreeMeshIndex : 16;

		/** Index to the water body that this tile possibly transitions to */
		uint32 TransitionQuadtreeMeshIndex : 16;

		/** Index to parent */
		uint32 ParentIndex : 28;

		/** If all 4 child nodes have a full set of leaf nodes (each descentant has 4 children all the way down) */
		uint32 HasCompleteSubtree : 1;

		/** If all descendant nodes are from the same waterbody. We can safely collapse this even if HasCompleteSubtree is false */
		uint32 IsSubtreeSameQuadtreeMesh : 1;

		/** Cached value to avoid having to visit this node's FWaterBodyRenderData */
		uint32 HasMaterial : 1;

		// 1 spare bits here in the bit field with ParentIndex

		/** Children, 0 means invalid */
		uint32 Children[4] = { 0, 0, 0, 0 };
	};


	struct FNodeData
	{
		/** Storage for all nodes in the tree. Each node has 4 indices into this array to locate its children */
		TArray<FNode> Nodes;

		/** Render data for all water bodies in this tree, indexed by the nodes */
		TArray<FQuadtreeMeshRenderData> QuadtreeMeshRenderData;

		/** Total memory dynamically allocated by this object */
		uint32 GetAllocatedSize() const { return Nodes.GetAllocatedSize() + QuadtreeMeshRenderData.GetAllocatedSize(); }
	} NodeData;
};



