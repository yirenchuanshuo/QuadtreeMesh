#pragma once
#include "ShaderParameterMacros.h"
#include "VertexFactory.h"
#include "Containers/DynamicRHIResourceArray.h"



class FQuadMeshVertexBuffer:public FVertexBuffer
{
public:
	FQuadMeshVertexBuffer(int32 InNumQuadsPerSide) : NumQuadsPerSide(InNumQuadsPerSide) {}

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override
	{
		ensureAlways(NumQuadsPerSide > 0);
		const uint32 NumVertsPerSide = NumQuadsPerSide + 1;

		NumVerts = NumVertsPerSide * NumVertsPerSide;

		FRHIResourceCreateInfo CreateInfo(TEXT("FQuadMeshVertexBuffer"));
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
