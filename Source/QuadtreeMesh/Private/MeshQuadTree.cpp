#include "MeshQuadTree.h"
#include<format>


void FMeshQuadTree::GatherHitProxies(TArray<TRefCountPtr<HHitProxy>>& OutHitProxies) const
{
	for(const FQuadtreeMeshRenderData& QuadtreeMeshRenderData : NodeData.QuadtreeMeshRenderData)
	{
		OutHitProxies.Add(QuadtreeMeshRenderData.HitProxy);
	}
}

void FMeshQuadTree::InitTree(const FBox2D& InBounds, float InTileSize, FIntPoint InExtentInTiles)
{
	ensure(InBounds.GetArea() > 0.0f);
	ensure(InTileSize > 0.0f);
	ensure(InExtentInTiles.X > 0);
	ensure(InExtentInTiles.Y > 0);

	// Maximum number of allocated leaf nodes for this config
	MaxLeafCount = InExtentInTiles.X*InExtentInTiles.Y*4;
	LeafSize = InTileSize;
	ExtentInTiles = InExtentInTiles;

	// Calculate the depth of the tree. This also corresponds to the LOD count. 0 means root is leaf node
	// Find a pow2 tile resolution that contains the user defined extent in tiles
	const int32 MaxDim = FMath::Max(InExtentInTiles.X * 2, InExtentInTiles.Y * 2);
	const float RootDim = static_cast<float>(FMath::RoundUpToPowerOfTwo(MaxDim));

	TileRegion = InBounds;

	// Allocate theoretical max, shrink later in Lock()
	// This is so that the node array doesn't move in memory while inserting
	NodeData.Nodes.Empty(FMath::Square(RootDim) * 4 / 3.0f);
	
	NodeData.QuadtreeMeshRenderData.Empty(1);
	NodeData.QuadtreeMeshRenderData.AddDefaulted();

	ensure(NodeData.Nodes.Num() == 0);

	// Add the root node at slot 0
	NodeData.Nodes.Emplace();

	const float RootWorldSize = RootDim * InTileSize;

	TreeDepth = static_cast<int32>(FMath::Log2(RootDim));

	// Init root node bounds with invalid Z since that will be updated as nodes are added to the tree
	NodeData.Nodes[0].Bounds = FBox(FVector(TileRegion.Min, TNumericLimits<float>::Max()), FVector(TileRegion.Min + FVector2D(RootWorldSize, RootWorldSize), TNumericLimits<float>::Lowest()));

	ensure(NodeData.Nodes.Num() == 1);

	bIsReadOnly = false;
}

void FMeshQuadTree::Unlock(bool bPruneRedundantNodes)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Unlock);

	if (bPruneRedundantNodes)
	{
		auto SwapRemove = [&](int32 NodeIndex, int32 EndIndex)
		{
			if (NodeIndex != EndIndex)
			{
				// Swap to back. All the children of this node would have already been removed (or didn't exist to begin with), so don't care about those
				NodeData.Nodes.SwapMemory(NodeIndex, EndIndex);

				// Patch up the newly moved good node (parent and children)
				FNode& MovedNode = NodeData.Nodes[NodeIndex];
				FNode& MovedNodeParent = NodeData.Nodes[MovedNode.ParentIndex];

				for (int32 i = 0; i < 4; i++)
				{
					if (MovedNode.Children[i] > 0)
					{
						NodeData.Nodes[MovedNode.Children[i]].ParentIndex = NodeIndex;
					}

					if (MovedNodeParent.Children[i] == EndIndex)
					{
						MovedNodeParent.Children[i] = NodeIndex;
					}
				}
			}
		};

		// Remove redundant nodes
		// Remove from the back, since all removalbe children are further back than their parent in the node list and we want to remove bottom-up
		int32 EndIndex = NodeData.Nodes.Num() - 1;
		for (int NodeIndex = EndIndex; NodeIndex > 0; NodeIndex--)
		{
			FNode& ParentNode = NodeData.Nodes[NodeData.Nodes[NodeIndex].ParentIndex];
			
			if (ParentNode.HasCompleteSubtree && ParentNode.IsSubtreeSameQuadtreeMesh)
			{
				// Delete all children (not strictly necessary, but now we don't leave any dangling/incorrect child pointers around)
				FMemory::Memzero(&ParentNode.Children, sizeof(uint32) * 4);

				SwapRemove(NodeIndex, EndIndex);

				// Move back one step down
				EndIndex--;
			}
			else if (!NodeData.Nodes[NodeIndex].HasMaterial && NodeData.Nodes[NodeIndex].HasCompleteSubtree && NodeData.Nodes[NodeIndex].IsSubtreeSameQuadtreeMesh)
			{
				for (int32 i = 0; i < 4; i++)
				{
					if (ParentNode.Children[i] == NodeIndex)
					{
						ParentNode.Children[i] = 0;
					}
				}

				SwapRemove(NodeIndex, EndIndex);

				// Move back one step down
				EndIndex--;
			}
		}

		NodeData.Nodes.SetNum(EndIndex + 1);
	}

	bIsReadOnly = true;
}

void FMeshQuadTree::AddQuadtreeMeshTilesInsideBounds(const FBox& InBounds, uint32 InQuadtreeMeshIndex)
{
	check(!bIsReadOnly);
	NodeData.Nodes[0].AddNodes(NodeData, FBox(FVector(TileRegion.Min, 0.0f), FVector(TileRegion.Max, 0.0f)),  InBounds, InQuadtreeMeshIndex, TreeDepth, 0);
}

void FMeshQuadTree::AddQuadtreeMesh(const TArray<FVector2D>& InPoly, const FBox& InMeshBounds, uint32 InQuadtreeMeshIndex)
{
	check(!bIsReadOnly);
	const FBox2D MeshBounds(FVector2D(InMeshBounds.Min), FVector2D(InMeshBounds.Max));
	const FVector2D ZBound = FVector2D(InMeshBounds.Min.Z, InMeshBounds.Max.Z);
	// If we are at the leaf level, add the node
	const FVector2D LeafSizeShrink(LeafSize * 0.25, LeafSize * 0.25);
	
	const FBox TileBounds(FVector(MeshBounds.Min + LeafSizeShrink, ZBound.X), FVector(MeshBounds.Max - LeafSizeShrink, ZBound.Y));
	AddQuadtreeMeshTilesInsideBounds(TileBounds, InQuadtreeMeshIndex);
	
}

void FMeshQuadTree::BuildMaterialIndices()
{
	int32 NextIdx = 0;
	TMap<FMaterialRenderProxy*, int32> MatToIdxMap;

	auto GetMatIdx = [&NextIdx, &MatToIdxMap](const UMaterialInterface* Material)
	{
		if (!Material)
		{
			return static_cast<int32>(INDEX_NONE);
		}
		FMaterialRenderProxy* MaterialRenderProxy = Material->GetRenderProxy();
		check(MaterialRenderProxy != nullptr);
		const int32* Found = MatToIdxMap.Find(MaterialRenderProxy);
		if (!Found)
		{
			Found = &MatToIdxMap.Add(MaterialRenderProxy, NextIdx++);
		}
		return *Found;
	};

	for (int32 Idx = 0; Idx < NodeData.QuadtreeMeshRenderData.Num(); ++Idx)
	{
		FQuadtreeMeshRenderData& Data = NodeData.QuadtreeMeshRenderData[Idx];
		Data.MaterialIndex = GetMatIdx(Data.Material);
	}

	

	QuadtreeMeshMaterials.Empty(MatToIdxMap.Num());
	QuadtreeMeshMaterials.AddUninitialized(MatToIdxMap.Num());

	for (TMap<FMaterialRenderProxy*, int32>::TConstIterator It(MatToIdxMap); It; ++It)
	{
		QuadtreeMeshMaterials[It->Value] = It->Key;
	}
}

void FMeshQuadTree::BuildQuadtreeMeshTileInstanceData(const FTraversalDesc& InTraversalDesc,
	FTraversalOutput& Output) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(BuildQuadtreeMeshTileInstanceData);
	check(bIsReadOnly);
	if (InTraversalDesc.TessellatedQuadtreeMeshBounds.bIsValid)
	{
		NodeData.Nodes[0].SelectLODWithinBounds(NodeData, TreeDepth, InTraversalDesc, Output);
	}
	else
	{
		NodeData.Nodes[0].SelectLOD(NodeData, TreeDepth, InTraversalDesc, Output);
	}
}

bool FMeshQuadTree::QueryInterpolatedTileBaseHeightAtLocation(const FVector2D& InWorldLocationXY,float& OutHeight) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshQuadTree::QueryInterpolatedTileBaseHeightAtLocation);

	// Figure out what 4 samples to take
	// Sample point grid is aligned with center of leaf node tiles. So offset the grid negative half a leaf tile
	const FVector2D SampleGridWorldPosition(GetTileRegion().Min - FVector2D(GetLeafSize() * 0.5f));
	const FVector2D CornerSampleGridPosition(InWorldLocationXY - SampleGridWorldPosition);
	const FVector2D NormalizedGridPosition(CornerSampleGridPosition / GetLeafSize());
	const FVector2D CornerSampleWorldPosition00 = FVector2D(FMath::Floor(NormalizedGridPosition.X), FMath::Floor(NormalizedGridPosition.Y)) * GetLeafSize() + SampleGridWorldPosition;
	
	// 4 world positions to use for sampling
	const FVector2D CornerSampleWorldPositions[] =
	{
		CornerSampleWorldPosition00 + FVector2D(0.0f, 0.0f),
		CornerSampleWorldPosition00 + FVector2D(GetLeafSize(), 0.0f),
		CornerSampleWorldPosition00 + FVector2D(0.0f, GetLeafSize()),
		CornerSampleWorldPosition00 + FVector2D(GetLeafSize(), GetLeafSize())
	};

	// Sample 4 locations
	float HeightSamples[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	int32 NumValidSamples = 0;
	for(int32 i = 0; i < 4; i++)
	{
		if (QueryTileBaseHeightAtLocation(CornerSampleWorldPositions[i], HeightSamples[i]))
		{
			NumValidSamples++;
		}
	}

	// Return bilinear interpolated value
	OutHeight = FMath::BiLerp(HeightSamples[0], HeightSamples[1], HeightSamples[2], HeightSamples[3], FMath::Frac(NormalizedGridPosition.X), FMath::Frac(NormalizedGridPosition.Y));

	return NumValidSamples == 4;
}

bool FMeshQuadTree::QueryTileBaseHeightAtLocation(const FVector2D& InWorldLocationXY, float& OutWorldHeight) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshQuadTree::QueryTileBaseHeightAtLocation);
	if (GetNodeCount() > 0)
	{
		check(bIsReadOnly);
		return NodeData.Nodes[0].QueryBaseHeightAtLocation(NodeData, InWorldLocationXY, OutWorldHeight);
	}
	
	OutWorldHeight = 0.0f;
	return false;
}

bool FMeshQuadTree::QueryTileBoundsAtLocation(const FVector2D& InWorldLocationXY, FBox& OutWorldBounds) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshQuadTree::QueryTileBoundsAtLocation);
	if (GetNodeCount() > 0)
	{
		check(bIsReadOnly);
		return NodeData.Nodes[0].QueryBoundsAtLocation(NodeData, InWorldLocationXY, OutWorldBounds);
	}

	OutWorldBounds = FBox(ForceInit);
	return false;
}


bool FMeshQuadTree::FNode::CanRender(int32 InDensityLevel, int32 InForceCollapseDensityLevel,
                                     const FQuadtreeMeshRenderData& InQuadtreeMeshRenderData) const
{
	return InQuadtreeMeshRenderData.Material && IsSubtreeSameQuadtreeMesh && ((InDensityLevel > InForceCollapseDensityLevel) || HasCompleteSubtree);
}

void FMeshQuadTree::FNode::SelectLODRefinement(const FNodeData& InNodeData, int32 InDensityLevel, int32 InLODLevel,
	const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const
{
	const FQuadtreeMeshRenderData& QuadtreeMeshRenderData = InNodeData.QuadtreeMeshRenderData[QuadtreeMeshIndex];
	const FVector CenterPosition = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();

	// Early out on frustum culling 
	if (InTraversalDesc.Frustum.IntersectBox(CenterPosition, Extent))
	{
		// This LOD can represent all its leaf nodes, simply add node
		if (CanRender(InDensityLevel, InTraversalDesc.ForceCollapseDensityLevel, QuadtreeMeshRenderData))
		{
			AddNodeForRender(InNodeData, QuadtreeMeshRenderData, InDensityLevel, InLODLevel, InTraversalDesc, Output);
		}
		else
		{
			// If not, we need to recurse down the children until we find one that can be rendered
			for (int32 ChildIndex : Children)
			{
				if (ChildIndex > 0)
				{
					InNodeData.Nodes[ChildIndex].SelectLODRefinement(InNodeData, InDensityLevel + 1, InLODLevel, InTraversalDesc, Output);
				}
			}
		}
	}
}

void FMeshQuadTree::FNode::SelectLOD(const FNodeData& InNodeData, int32 InLODLevel,
                                     const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const
{
	const FQuadtreeMeshRenderData& QuadtreeMeshRenderData = InNodeData.QuadtreeMeshRenderData[QuadtreeMeshIndex];
	const FVector CenterPosition = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();

	// Early out on frustum culling 
	if (!InTraversalDesc.Frustum.IntersectBox(CenterPosition, Extent))
	{
		// Handled
		return;
	}

	// Distance to tile (if 0, position is inside quad)
	FBox2D Bounds2D(FVector2D(Bounds.Min), FVector2D(Bounds.Max));
	const float ClosestDistanceToTile = FMath::Sqrt(Bounds2D.ComputeSquaredDistanceToPoint(FVector2D(InTraversalDesc.ObserverPosition)));

	// If quad is outside this LOD range, it belongs to the LOD above, assume it fits in that LOD and drill down to find renderable nodes
	if (ClosestDistanceToTile > GetLODDistance(InLODLevel, InTraversalDesc.LODScale))
	{
		// This node is capable of representing all its leaf nodes, so just submit this node
		if (CanRender(0, InTraversalDesc.ForceCollapseDensityLevel, QuadtreeMeshRenderData))
		{
			AddNodeForRender(InNodeData, QuadtreeMeshRenderData, 1, InLODLevel + 1, InTraversalDesc, Output);
		}
		else
		{
			// If not, we need to recurse down the children until we find one that can be rendered
			for (int32 ChildIndex : Children)
			{
				if (ChildIndex > 0)
				{
					InNodeData.Nodes[ChildIndex].SelectLODRefinement(InNodeData, 2, InLODLevel + 1, InTraversalDesc, Output);
				}
			}
		}

		// Handled
		return;
	}

	// Last LOD, simply add node
	if (InLODLevel == 0)
	{
		if (CanRender(0, InTraversalDesc.ForceCollapseDensityLevel, QuadtreeMeshRenderData))
		{
			AddNodeForRender(InNodeData, QuadtreeMeshRenderData, 0, InLODLevel, InTraversalDesc, Output);
		}
	}
	else
	{
		// This quad is fully inside its LOD (also qualifies if it's simply the lowest LOD)
		if (ClosestDistanceToTile > GetLODDistance(InLODLevel - 1, InTraversalDesc.LODScale) || InLODLevel == InTraversalDesc.LowestLOD)
		{
			// This node is capable of representing all its leaf nodes, so just submit this node
			if (CanRender(0, InTraversalDesc.ForceCollapseDensityLevel, QuadtreeMeshRenderData))
			{
				AddNodeForRender(InNodeData, QuadtreeMeshRenderData, 0, InLODLevel, InTraversalDesc, Output);
			}
			else
			{
				// If not, we need to recurse down the children until we find one that can be rendered
				for (int32 ChildIndex : Children)
				{
					if (ChildIndex > 0)
					{
						InNodeData.Nodes[ChildIndex].SelectLODRefinement(InNodeData, 1, InLODLevel, InTraversalDesc, Output);
					}
				}
			}
		}
		else
		{
			// If this node has a complete subtree it will not contain any actual children, they are implicit to save memory so we generate them here
			if (HasCompleteSubtree && IsSubtreeSameQuadtreeMesh)
			{
				FNode ChildNode;
				const FVector HalfBoundSize(Extent.X, Extent.Y, Extent.Z*2.0f);
				const FVector HalfOffsets[] = { {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} , {0.0f, 1.0f, 0.0f} , {1.0f, 1.0f, 0.0f} };
				for (int i = 0; i < 4; i++)
				{
					const FVector ChildMin = Bounds.Min + HalfBoundSize * HalfOffsets[i];
					const FVector ChildMax = ChildMin + HalfBoundSize;
					const FBox ChildBounds(ChildMin, ChildMax);

					// Create a temporary node to traverse
					ChildNode.HasCompleteSubtree = 1;
					ChildNode.IsSubtreeSameQuadtreeMesh = 1;
					ChildNode.TransitionQuadtreeMeshIndex = TransitionQuadtreeMeshIndex;
					ChildNode.QuadtreeMeshIndex = QuadtreeMeshIndex;
					ChildNode.Bounds = ChildBounds;

					ChildNode.SelectLOD(InNodeData, InLODLevel - 1, InTraversalDesc, Output);
				}
			}
			else
			{
				for (int32 ChildIndex : Children)
				{
					if (ChildIndex > 0)
					{
						InNodeData.Nodes[ChildIndex].SelectLOD(InNodeData, InLODLevel - 1, InTraversalDesc, Output);
					}
				}
			}
		}
	}
}

void FMeshQuadTree::FNode::SelectLODWithinBounds(const FNodeData& InNodeData, int32 InLODLevel,
                                                 const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const
{
	const FQuadtreeMeshRenderData& QuadtreeMeshRenderData = InNodeData.QuadtreeMeshRenderData[QuadtreeMeshIndex];
	const FVector CenterPosition = Bounds.GetCenter();
	const FVector Extent = Bounds.GetExtent();

	// Early out on frustum culling 
	if (!InTraversalDesc.Frustum.IntersectBox(CenterPosition, Extent))
	{
		// Handled
		return;
	}

	check(InTraversalDesc.TessellatedQuadtreeMeshBounds.bIsValid);
	if (InLODLevel == 0)
	{
		if ((InTraversalDesc.TessellatedQuadtreeMeshBounds.IsInsideOrOn(FVector2D(Bounds.Min)) && InTraversalDesc.TessellatedQuadtreeMeshBounds.IsInsideOrOn(FVector2D(Bounds.Max))) &&
			CanRender(0, InTraversalDesc.ForceCollapseDensityLevel, QuadtreeMeshRenderData))
		{
			AddNodeForRender(InNodeData, QuadtreeMeshRenderData, 0, InLODLevel, InTraversalDesc, Output);
		}
	}
	else
	{
		// If this node has a complete subtree it will not contain any actual children, they are implicit to save memory so we generate them here
		if (HasCompleteSubtree && IsSubtreeSameQuadtreeMesh)
		{
			FNode ChildNode;
			const FVector HalfBoundSize(Extent.X, Extent.Y, Extent.Z*2.0f);
			const FVector HalfOffsets[] = { {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} , {0.0f, 1.0f, 0.0f} , {1.0f, 1.0f, 0.0f} };
			for (int i = 0; i < 4; i++)
			{
				const FVector ChildMin = Bounds.Min + HalfBoundSize * HalfOffsets[i];
				const FVector ChildMax = ChildMin + HalfBoundSize;
				const FBox ChildBounds(ChildMin, ChildMax);

				// Create a temporary node to traverse
				ChildNode.HasCompleteSubtree = 1;
				ChildNode.IsSubtreeSameQuadtreeMesh = 1;
				ChildNode.TransitionQuadtreeMeshIndex = TransitionQuadtreeMeshIndex;
				ChildNode.QuadtreeMeshIndex = QuadtreeMeshIndex;
				ChildNode.Bounds = ChildBounds;

				ChildNode.SelectLODWithinBounds(InNodeData, InLODLevel - 1, InTraversalDesc, Output);
			}
		}
		else
		{
			for (const int32 ChildIndex : Children)
			{
				if (ChildIndex > 0)
				{
					InNodeData.Nodes[ChildIndex].SelectLODWithinBounds(InNodeData, InLODLevel - 1, InTraversalDesc, Output);
				}
			}
		}
	}
}

bool FMeshQuadTree::FNode::QueryBaseHeightAtLocation(const FNodeData& InNodeData, const FVector2D& InWorldLocationXY,float& OutHeight) const
{
	// Note: Since we prune the quadtree of anything below this condition, it means there are no more granular nodes to fetch below this. In theory we could skip the pruning and have slightly more accurate height sampling, since rivers might have leaf nodes with individual bounds.
	// Same condition as leaf nodes
	if (HasCompleteSubtree && IsSubtreeSameQuadtreeMesh)
	{
		// Return "accurate" base height when there's a valid sample
		OutHeight = InNodeData.QuadtreeMeshRenderData[QuadtreeMeshIndex].SurfaceBaseHeight;
		
		return true;
	}

	for (const int32 ChildIndex : Children)
	{
		if (ChildIndex > 0)
		{
			const FNode& ChildNode = InNodeData.Nodes[ChildIndex];
			const FBox ChildBounds = ChildNode.Bounds;

			// Check if point is inside (or on the Min edges) of the child bounds
			if ((InWorldLocationXY.X >= ChildBounds.Min.X) && (InWorldLocationXY.X < ChildBounds.Max.X)
				&& (InWorldLocationXY.Y >= ChildBounds.Min.Y) && (InWorldLocationXY.Y < ChildBounds.Max.Y))
			{
				return ChildNode.QueryBaseHeightAtLocation(InNodeData, InWorldLocationXY, OutHeight);
			}
		}
	}

	// Return regular base height when there's not valid sample
	OutHeight = InNodeData.QuadtreeMeshRenderData[QuadtreeMeshIndex].SurfaceBaseHeight;

	// Point is not in any of these children, return false
	return false;
}

bool FMeshQuadTree::FNode::QueryBoundsAtLocation(const FNodeData& InNodeData, const FVector2D& InWorldLocationXY,FBox& OutBounds) const
{
	OutBounds = Bounds;

	int32 ChildCount = 0;
	for (const int32 ChildIndex : Children)
	{
		if (ChildIndex > 0)
		{
			ChildCount++;
			const FNode& ChildNode = InNodeData.Nodes[ChildIndex];
			const FBox ChildBounds = ChildNode.Bounds;

			// Check if point is inside (or on the Min edges) of the child bounds
			if ((InWorldLocationXY.X >= ChildBounds.Min.X) && (InWorldLocationXY.X < ChildBounds.Max.X)
				&& (InWorldLocationXY.Y >= ChildBounds.Min.Y) && (InWorldLocationXY.Y < ChildBounds.Max.Y))
			{
				return ChildNode.QueryBoundsAtLocation(InNodeData, InWorldLocationXY, OutBounds);
			}
		}
	}

	// No children, this is a leaf node, return true. Otherwise reaching here means none of the children contain the sampling location, so return false
	return ChildCount == 0;
}

void FMeshQuadTree::FNode::AddNodes(FNodeData& InNodeData, const FBox& InMeshBounds, const FBox& InQuadtreeMeshBounds,
                                    uint32 InQuadtreeMeshIndex, int32 InLODLevel, uint32 InParentIndex)
{
	// Update the bounds
	Bounds.Max.Z = FMath::Max(Bounds.Max.Z, InQuadtreeMeshBounds.Max.Z);
	Bounds.Min.Z = FMath::Min(Bounds.Min.Z, InQuadtreeMeshBounds.Min.Z);

	TransitionQuadtreeMeshIndex = static_cast<uint16>(InQuadtreeMeshIndex);
	

	// Assign the render data here (based on priority)
	QuadtreeMeshIndex = InQuadtreeMeshIndex;
	// Cache whether or not this node has a material
	HasMaterial = InNodeData.QuadtreeMeshRenderData[QuadtreeMeshIndex].Material != nullptr;
	

	// Reset the flags before going through the children. These flags will be turned off by recursion if the state changes
	// Setting them here ensures leaf nodes are marked as complete subtrees, allowing them to be further implicitly subdivided
	IsSubtreeSameQuadtreeMesh = 1;
	HasCompleteSubtree = 1;

	// This is a leaf node, stop here
	if (InLODLevel == 0)
	{
		return;
	}

	const FVector2D HalfBoundSize = FVector2D(Bounds.GetSize()) * 0.5f;

	FNode PrevChildNode = InNodeData.Nodes[0];
	const FVector2D HalfOffsets[] = { {0.0f, 0.0f}, {1.0f, 0.0f} , {0.0f, 1.0f} , {1.0f, 1.0f} };
	for (int32 i = 0; i < 4; i++)
	{
		if (Children[i] > 0)
		{
			if (InNodeData.Nodes[Children[i]].Bounds.IntersectXY(InQuadtreeMeshBounds))
			{
				InNodeData.Nodes[Children[i]].AddNodes(InNodeData, InMeshBounds, InQuadtreeMeshBounds, InQuadtreeMeshIndex, InLODLevel - 1, Children[i]);
			}
		}
		else
		{
			// Check if this child needs to be created. If yes, initialize it with the depth bounds of InBounds
			const FVector ChildMin(FVector2D(Bounds.Min) + HalfBoundSize * HalfOffsets[i], InQuadtreeMeshBounds.Min.Z);
			const FVector ChildMax(FVector2D(ChildMin) + HalfBoundSize, InQuadtreeMeshBounds.Max.Z);
			const FBox ChildBounds(ChildMin, ChildMax);

			if (ChildBounds.IntersectXY(InQuadtreeMeshBounds) && ChildBounds.IntersectXY(InMeshBounds))
			{
				// All nodes have been allocated upfront, no reallocation should occur : 
				check(InNodeData.Nodes.Num() < InNodeData.Nodes.Max());
				Children[i] = InNodeData.Nodes.Emplace();
				InNodeData.Nodes[Children[i]].Bounds = ChildBounds; 
				InNodeData.Nodes[Children[i]].ParentIndex = InParentIndex;
				InNodeData.Nodes[Children[i]].AddNodes(InNodeData, InMeshBounds, InQuadtreeMeshBounds, InQuadtreeMeshIndex, InLODLevel - 1, Children[i]);
			}
		}

		if (Children[i] > 0)
		{
			const FNode& ChildNode = InNodeData.Nodes[Children[i]];

			// If INVALID_PARENT, compare against current since there are no previous children
			PrevChildNode = (PrevChildNode.ParentIndex == INVALID_PARENT ? ChildNode : PrevChildNode);

			
			if (ChildNode.IsSubtreeSameQuadtreeMesh == 0 || !ChildNode.CanMerge(PrevChildNode))
			{
				IsSubtreeSameQuadtreeMesh = 0;
			}

			PrevChildNode = ChildNode;

			if (ChildNode.HasCompleteSubtree == 0)
			{
				HasCompleteSubtree = 0;
			}
		}
		else
		{
			HasCompleteSubtree = 0;
		}
	}
}

void FMeshQuadTree::FNode::AddNodeForRender(const FNodeData& InNodeData,
	const FQuadtreeMeshRenderData& InQuadtreeMeshRenderData, int32 InDensityLevel, int32 InLODLevel,
	const FTraversalDesc& InTraversalDesc, FTraversalOutput& Output) const
{
	constexpr int32 MaterialIndex = 0;
	constexpr  uint32 NodeQuadtreeMeshIndex = 2;
	

	// The base height of this tile comes either the top of the bounding box (for rivers) or the given base height (lakes and ocean)
	const double BaseHeight = InQuadtreeMeshRenderData.SurfaceBaseHeight;

	const float BaseHeightTWS = BaseHeight + InTraversalDesc.PreViewTranslation.Z;

	const int32 DensityIndex = FMath::Min(InDensityLevel, InTraversalDesc.DensityCount - 1);
	const int32 BucketIndex = MaterialIndex * InTraversalDesc.DensityCount + DensityIndex;
	
	++Output.BucketInstanceCounts[BucketIndex];

	const FVector TranslatedWorldPosition(Bounds.GetCenter() + InTraversalDesc.PreViewTranslation);
	const FVector2D Scale(Bounds.GetSize());
	FStagingInstanceData& StagingData = Output.StagingInstanceData[Output.StagingInstanceData.AddUninitialized()];

	// Add the data to the bucket
	StagingData.BucketIndex = BucketIndex;
	StagingData.Data[0].X = TranslatedWorldPosition.X;
	StagingData.Data[0].Y = TranslatedWorldPosition.Y;
	StagingData.Data[0].Z = BaseHeightTWS;
	//StagingData.Data[0].W = *(float*)&NodeQuadtreeMeshIndex;
	StagingData.Data[0].W = std::bit_cast<float>(NodeQuadtreeMeshIndex);

	// Lowest LOD isn't always 0, this increases with the height distance 
	const bool bIsLowestLOD = (InLODLevel == InTraversalDesc.LowestLOD);

	// Only allow a tile to morph if it's not the last density level and not the last LOD level, sicne there is no next level to morph to
	const uint32 bShouldMorph = (InTraversalDesc.bLODMorphingEnabled && (DensityIndex != InTraversalDesc.DensityCount - 1)) ? 1 : 0;
	// Tiles can morph twice to be able to morph between 3 LOD levels. Next to last density level can only morph once
	const uint32 bCanMorphTwice = (DensityIndex < InTraversalDesc.DensityCount - 2) ? 1 : 0;

	// Pack some of the data to save space. LOD level in the lower 8 bits and then bShouldMorph in the 9th bit and bCanMorphTwice in the 10th bit
	const uint32 BitPackedChannel = (static_cast<uint32>(InLODLevel) & 0xFF) | (bShouldMorph << 8) | (bCanMorphTwice << 9);

	// Should morph
	//StagingData.Data[1].X = *(float*)&BitPackedChannel;
	StagingData.Data[1].X = std::bit_cast<float>(BitPackedChannel);
	StagingData.Data[1].Y = bIsLowestLOD ? InTraversalDesc.HeightMorph : 0.0f;
	StagingData.Data[1].Z = Scale.X;
	StagingData.Data[1].W = Scale.Y;


	// Instance Hit Proxy ID
	const FLinearColor HitProxyColor = InQuadtreeMeshRenderData.HitProxy->Id.GetColor().ReinterpretAsLinear();
	StagingData.Data[2].X = HitProxyColor.R;
	StagingData.Data[2].Y = HitProxyColor.G;
	StagingData.Data[2].Z = HitProxyColor.B;
	StagingData.Data[2].W = InQuadtreeMeshRenderData.bQuadtreeMeshSelected ? 1.0f : 0.0f;


	++Output.InstanceCount;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Debug drawing
	
	if (InTraversalDesc.DebugShowTile != 0)
	{
		FColor Color;
		if (InTraversalDesc.DebugShowTile == 1)
		{
			constexpr  int32 TileDebugID = 2;
			static FColor QuadtreeMeshTypeColor[] = { FColor::Red, FColor::Green, FColor::Blue, FColor::Yellow, FColor::Purple };
			Color = QuadtreeMeshTypeColor[TileDebugID];
		}
		else if (InTraversalDesc.DebugShowTile == 2)
		{
			Color = GColorList.GetFColorByIndex(InLODLevel + 1);
		}
		else if (InTraversalDesc.DebugShowTile == 3)
		{

			Color = GColorList.GetFColorByIndex(DensityIndex + 1);
		}

		DrawWireBox(InTraversalDesc.DebugPDI, Bounds.ExpandBy(FVector(-20.0f, -20.0f, 0.0f)), Color, SDPG_World);
	}
#endif
}

