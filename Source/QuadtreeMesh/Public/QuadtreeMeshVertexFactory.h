#pragma once
#include "ShaderParameterMacros.h"
#include "VertexFactory.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "QuadtreeMeshInstanceDataBuffer.h"

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FQuadtreeMeshVertexFactoryParameters, )
	SHADER_PARAMETER(float, LODScale)
	SHADER_PARAMETER(int32, NumQuadsPerTileSide)
	SHADER_PARAMETER(int32, bRenderSelected)
	SHADER_PARAMETER(int32, bRenderUnselected)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
using FQuadtreeMeshVertexFactoryBufferRef = TUniformBufferRef<FQuadtreeMeshVertexFactoryParameters>;

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FQuadtreeMeshVertexFactoryRaytracingParameters, )
	SHADER_PARAMETER_SRV(Buffer<float>, VertexBuffer)
	SHADER_PARAMETER(FVector4f, InstanceData0)
	SHADER_PARAMETER(FVector4f, InstanceData1)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
using FQuadtreeMeshVertexFactoryRaytracingParametersRef = TUniformBufferRef<FQuadtreeMeshVertexFactoryRaytracingParameters>;

class FQuadtreeMeshIndexBuffer : public FIndexBuffer
{
public:
	FQuadtreeMeshIndexBuffer(int32 InNumQuadsPerSide) : NumQuadsPerSide(InNumQuadsPerSide) {}
	void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		// This is an optimized index buffer path for water tiles containing less than uint16 max vertices
		if (NumQuadsPerSide < 256)
		{
			IndexBufferRHI = CreateIndexBuffer<uint16>(RHICmdList);
		}
		else
		{
			IndexBufferRHI = CreateIndexBuffer<uint32>(RHICmdList);
		}
	}
	
private:
	
	template <typename IndexType>
	FBufferRHIRef CreateIndexBuffer(FRHICommandListBase& RHICmdList)
	{
		TResourceArray<IndexType, INDEXBUFFER_ALIGNMENT> Indices;

		// Allocate room for indices
		Indices.Reserve(NumQuadsPerSide * NumQuadsPerSide * 6);

		// Build index buffer in morton order for better vertex reuse. This amounts to roughly 75% reuse rate vs 66% of naive scanline approach
		for (int32 Morton = 0; Morton < NumQuadsPerSide * NumQuadsPerSide; Morton++)
		{
			int32 SquareX = FMath::ReverseMortonCode2(Morton);
			int32 SquareY = FMath::ReverseMortonCode2(Morton >> 1);

			bool ForwardDiagonal = false;

			if (SquareX % 2)
			{
				ForwardDiagonal = !ForwardDiagonal;
			}
			if (SquareY % 2)
			{
				ForwardDiagonal = !ForwardDiagonal;
			}

			int32 Index0 = SquareX + SquareY * (NumQuadsPerSide + 1);
			int32 Index1 = Index0 + 1;
			int32 Index2 = Index0 + (NumQuadsPerSide + 1);
			int32 Index3 = Index2 + 1;

			Indices.Add(Index3);
			Indices.Add(Index1);
			Indices.Add(ForwardDiagonal ? Index2 : Index0);
			Indices.Add(Index0);
			Indices.Add(Index2);
			Indices.Add(ForwardDiagonal ? Index1 : Index3);
		}

		NumIndices = Indices.Num();
		const uint32 Size = Indices.GetResourceDataSize();
		const uint32 Stride = sizeof(IndexType);

		// Create index buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(TEXT("FQuadtreeMeshIndexBuffer"), &Indices);
		return RHICmdList.CreateIndexBuffer(Stride, Size, BUF_Static, CreateInfo);
	}
		
	const int32 NumQuadsPerSide = 0;
	int32 NumIndices = 0;
};

class FQuadtreeMeshVertexBuffer:public FVertexBuffer
{
public:
	FQuadtreeMeshVertexBuffer(int32 InNumQuadsPerSide) : NumQuadsPerSide(InNumQuadsPerSide) {}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		ensureAlways(NumQuadsPerSide > 0);
		const uint32 NumVertsPerSide = NumQuadsPerSide + 1;

		NumVerts = NumVertsPerSide * NumVertsPerSide;

		FRHIResourceCreateInfo CreateInfo(TEXT("FQuadtreeMeshVertexBuffer"));
		VertexBufferRHI = RHICmdList.CreateBuffer(sizeof(FVector4f) * NumVerts, BUF_Static | BUF_VertexBuffer | BUF_ShaderResource, 0, ERHIAccess::VertexOrIndexBuffer | ERHIAccess::SRVMask, CreateInfo);
		FVector4f* DummyContents = static_cast<FVector4f*>(RHICmdList.LockBuffer(VertexBufferRHI, 0, sizeof(FVector4f) * NumVerts, RLM_WriteOnly));

		for (uint32 VertY = 0; VertY < NumVertsPerSide; VertY++)
		{
			FVector4f VertPos;
			VertPos.Y = static_cast<float>(VertY) / NumQuadsPerSide - 0.5f;

			for (uint32 VertX = 0; VertX < NumVertsPerSide; VertX++)
			{
				VertPos.X = static_cast<float>(VertX) / NumQuadsPerSide - 0.5f;

				DummyContents[NumVertsPerSide * VertY + VertX] = VertPos;
			}
			
		}

		RHICmdList.UnlockBuffer(VertexBufferRHI);

		SRV = RHICmdList.CreateShaderResourceView(VertexBufferRHI, sizeof(float), PF_R32_FLOAT);
	}

	virtual void ReleaseRHI() override
	{
		SRV.SafeRelease();
		FVertexBuffer::ReleaseRHI();
	}

	int32 GetVertexCount() const { return NumVerts; }
	FRHIShaderResourceView* GetSRV() { return SRV; }

private:
	int32 NumVerts = 0;
	const int32 NumQuadsPerSide = 0;

	FShaderResourceViewRHIRef SRV;
};


enum class EQuadtreeMeshRenderGroupType : uint8
{
	RG_RenderQuadtreeMeshTiles = 0,				// Render all QuadtreeMesh bodies

#if WITH_QUADTREEMESH_SELECTION_SUPPORT
	RG_RenderSelectedQuadtreeMeshTilesOnly,		// Render only selected QuadtreeMesh bodies
	RG_RenderUnselectedQuadtreeMeshTilesOnly,		// Render only unselected QuadtreeMesh bodies
#endif 
};

template <bool bWithQuadtreeMeshSelectionSupport>
class TQuadtreeMeshVertexFactory : public FVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(TQuadtreeMeshVertexFactory<bWithQuadtreeMeshSelectionSupport>);
public:
	using Super = FVertexFactory;
	static constexpr int32 NumRenderGroups = bWithQuadtreeMeshSelectionSupport ? 3 : 1; // Must match EWaterMeshRenderGroupType
	static constexpr int32 NumAdditionalVertexStreams = TQuadtreeMeshInstanceDataBuffers<bWithQuadtreeMeshSelectionSupport>::NumBuffers;
	
	TQuadtreeMeshVertexFactory(ERHIFeatureLevel::Type InFeatureLevel, int32 InNumQuadsPerSide,	float InLODScale);
	~TQuadtreeMeshVertexFactory();

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;

	virtual void ReleaseRHI() override;

	static bool ShouldCompilePermutation(const FVertexFactoryShaderPermutationParameters& Parameters);

	static void ModifyCompilationEnvironment(const FVertexFactoryShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment);

	static void GetPSOPrecacheVertexFetchElements(EVertexInputStreamType VertexInputStreamType, FVertexDeclarationElementList& Elements);

	inline const FUniformBufferRHIRef GetQuadtreeMeshVertexFactoryUniformBuffer(EQuadtreeMeshRenderGroupType InRenderGroupType) const { return UniformBuffers[static_cast<int32>(InRenderGroupType)]; }

private:
	void SetupUniformDataForGroup(EQuadtreeMeshRenderGroupType InRenderGroupType);

public:
	FQuadtreeMeshVertexBuffer* VertexBuffer = nullptr;
	FQuadtreeMeshIndexBuffer* IndexBuffer = nullptr;

private:
	TStaticArray<FQuadtreeMeshVertexFactoryBufferRef, NumRenderGroups> UniformBuffers;

	const int32 NumQuadsPerSide = 0;
	const float LODScale = 0.0f;
};


extern const FVertexFactoryType* GetQuadtreeMeshVertexFactoryType(bool bWithWaterSelectionSupport);

template <bool bWithQuadtreeMeshSelectionSupport>
struct TQuadtreeMeshUserData
{
	TQuadtreeMeshUserData() = default;

	TQuadtreeMeshUserData(EQuadtreeMeshRenderGroupType InRenderGroupType, const TQuadtreeMeshInstanceDataBuffers<bWithQuadtreeMeshSelectionSupport>* InInstanceDataBuffers)
		: RenderGroupType(InRenderGroupType)
		, InstanceDataBuffers(InInstanceDataBuffers)
	{
	}

	EQuadtreeMeshRenderGroupType RenderGroupType = EQuadtreeMeshRenderGroupType::RG_RenderQuadtreeMeshTiles;
	const TQuadtreeMeshInstanceDataBuffers<bWithQuadtreeMeshSelectionSupport>* InstanceDataBuffers = nullptr;

#if RHI_RAYTRACING	
	FUniformBufferRHIRef QuadtreeMeshVertexFactoryRaytracingVFUniformBuffer = nullptr;
#endif
};

#include "QuadtreeMeshVertexFactory.inl"

