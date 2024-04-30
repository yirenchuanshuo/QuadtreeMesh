#include "QuadtreeMeshActorDesc.h"
#include "QuadtreeMeshActor.h"

FQuadtreeMeshActorDesc::FQuadtreeMeshActorDesc()
	:OverlapPriority(0)
{
}

void FQuadtreeMeshActorDesc::Init(const AActor* InActor)
{
	FWorldPartitionActorDesc::Init(InActor);

	if (const AQuadtreeMeshActor* QuadtreeMesh = CastChecked<AQuadtreeMeshActor>(InActor))
	{
		OverlapPriority = QuadtreeMesh->GetOverlapPriority();
	}
}

bool FQuadtreeMeshActorDesc::Equals(const FWorldPartitionActorDesc* Other) const
{
	return FWorldPartitionActorDesc::Equals(Other);
}

void FQuadtreeMeshActorDesc::Serialize(FArchive& Ar)
{
	FWorldPartitionActorDesc::Serialize(Ar);
}
