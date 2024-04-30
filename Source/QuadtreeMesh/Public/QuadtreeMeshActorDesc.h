#pragma once

#if WITH_EDITOR
#include "WorldPartition/WorldPartitionActorDesc.h"

class FQuadtreeMeshActorDesc : public FWorldPartitionActorDesc
{
public:
	FQuadtreeMeshActorDesc();
	virtual void Init(const AActor* InActor) override;
	virtual bool Equals(const FWorldPartitionActorDesc* Other) const override;

	int32 GetOverlapPriority() const { return OverlapPriority; }
protected:
	virtual uint32 GetSizeOf() const override { return sizeof(FQuadtreeMeshActorDesc); }
	virtual void Serialize(FArchive& Ar) override;

	int32 OverlapPriority;
};

#endif
