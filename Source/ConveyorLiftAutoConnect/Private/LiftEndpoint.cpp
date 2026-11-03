#include "LiftEndpoint.h"

#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "FGRecipe.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorAttachmentHologram.h"

namespace
{
	UFGFactoryConnectionComponent* FindFactoryConnection(AFGBuildable* Buildable, FName Name)
	{
		if (!IsValid(Buildable) || Name.IsNone())
		{
			return nullptr;
		}
		TInlineComponentArray<UFGFactoryConnectionComponent*> Connections;
		Buildable->GetComponents(Connections);
		for (UFGFactoryConnectionComponent* Connection : Connections)
		{
			if (IsValid(Connection) && Connection->GetFName() == Name)
			{
				return Connection;
			}
		}
		return nullptr;
	}

	// Port names of an attachment's lift ports by facing: the port named mLiftConnection_Bottom takes lifts going up.
	FName GetAttachmentPortName(ELiftFacing Facing)
	{
		return Facing == ELiftFacing::Up
			? AFGConveyorAttachmentHologram::mLiftConnection_Bottom
			: AFGConveyorAttachmentHologram::mLiftConnection_Top;
	}

	// Both lift ports of a conveyor attachment, when it has two distinct vertical ones.
	bool TryGetAttachmentPorts(AFGBuildable* Buildable, UFGFactoryConnectionComponent*& OutUp, UFGFactoryConnectionComponent*& OutDown)
	{
		OutUp = nullptr;
		OutDown = nullptr;
		if (!IsValid(Cast<AFGBuildableConveyorAttachment>(Buildable)))
		{
			return false;
		}
		UFGFactoryConnectionComponent* Up = FindFactoryConnection(Buildable, GetAttachmentPortName(ELiftFacing::Up));
		UFGFactoryConnectionComponent* Down = FindFactoryConnection(Buildable, GetAttachmentPortName(ELiftFacing::Down));
		if (!IsValid(Up) || !IsValid(Down) || Up == Down || !Up->IsConnectionVertical() || !Down->IsConnectionVertical())
		{
			return false;
		}
		OutUp = Up;
		OutDown = Down;
		return true;
	}
}

bool LiftAutoConnect::IsTransportDirection(EFactoryConnectionDirection Direction)
{
	return Direction == EFactoryConnectionDirection::FCD_INPUT || Direction == EFactoryConnectionDirection::FCD_OUTPUT;
}

EFactoryConnectionDirection LiftAutoConnect::Opposite(EFactoryConnectionDirection Direction)
{
	switch (Direction)
	{
	case EFactoryConnectionDirection::FCD_INPUT:
		return EFactoryConnectionDirection::FCD_OUTPUT;
	case EFactoryConnectionDirection::FCD_OUTPUT:
		return EFactoryConnectionDirection::FCD_INPUT;
	default:
		return EFactoryConnectionDirection::FCD_ANY;
	}
}

ELiftFacing LiftAutoConnect::Opposite(ELiftFacing Facing)
{
	return Facing == ELiftFacing::Up ? ELiftFacing::Down : ELiftFacing::Up;
}

bool LiftAutoConnect::IsConveyorFloorHole(const AFGBuildablePassthrough* Hole)
{
	if (!IsValid(Hole))
	{
		return false;
	}
	const TSubclassOf<UFGConnectionComponent> ConnectionClass = Hole->GetConnectionClass();
	return ConnectionClass && ConnectionClass->IsChildOf(UFGFactoryConnectionComponent::StaticClass());
}

UFGFactoryConnectionComponent* LiftAutoConnect::GetFloorHoleSnapped(AFGBuildablePassthrough* Hole, ELiftFacing Side)
{
	if (!IsValid(Hole))
	{
		return nullptr;
	}
	return Side == ELiftFacing::Up
		? Hole->GetTopSnappedConnection<UFGFactoryConnectionComponent>()
		: Hole->GetBottomSnappedConnection<UFGFactoryConnectionComponent>();
}

void LiftAutoConnect::SetFloorHoleSnapped(AFGBuildablePassthrough* Hole, ELiftFacing Side, UFGFactoryConnectionComponent* Connection)
{
	if (!IsValid(Hole))
	{
		return;
	}
	if (Side == ELiftFacing::Up)
	{
		Hole->SetTopSnappedConnection(Connection);
	}
	else
	{
		Hole->SetBottomSnappedConnection(Connection);
	}
}

FTransform LiftAutoConnect::GetBuildableTransform(const AFGBuildable* Buildable, const AFGBlueprintHologram* Preview)
{
	if (!IsValid(Buildable))
	{
		return FTransform::Identity;
	}
	if (IsValid(Preview))
	{
		const TObjectPtr<USceneComponent>* Root = Preview->mBuildableToNewRoot.Find(const_cast<AFGBuildable*>(Buildable));
		if (Root && IsValid(Root->Get()))
		{
			return Root->Get()->GetComponentTransform();
		}
	}
	return Buildable->GetActorTransform();
}

const TCHAR* LiftAutoConnect::ToString(ELiftFlow Flow)
{
	switch (Flow)
	{
	case ELiftFlow::Up:
		return TEXT("up");
	case ELiftFlow::Down:
		return TEXT("down");
	default:
		return TEXT("unknown");
	}
}

bool FLiftEndpoint::IsSupportedBuildable(const AFGBuildable* Buildable)
{
	if (LiftAutoConnect::IsConveyorFloorHole(Cast<AFGBuildablePassthrough>(Buildable)))
	{
		return true;
	}
	UFGFactoryConnectionComponent* Up;
	UFGFactoryConnectionComponent* Down;
	return TryGetAttachmentPorts(const_cast<AFGBuildable*>(Buildable), Up, Down);
}

void FLiftEndpoint::Gather(AFGBuildable* Buildable, AFGBlueprintHologram* Preview, bool bOpenOnly, TArray<FLiftEndpoint>& OutEndpoints)
{
	if (AFGBuildablePassthrough* Hole = Cast<AFGBuildablePassthrough>(Buildable))
	{
		if (!LiftAutoConnect::IsConveyorFloorHole(Hole))
		{
			return;
		}
		for (const ELiftFacing Side : {ELiftFacing::Down, ELiftFacing::Up})
		{
			if (bOpenOnly && IsValid(LiftAutoConnect::GetFloorHoleSnapped(Hole, Side)))
			{
				continue;
			}
			OutEndpoints.Add({Buildable, ELiftEndpointKind::FloorHole, Side, NAME_None, Preview});
		}
		return;
	}

	UFGFactoryConnectionComponent* Ports[2];
	if (!TryGetAttachmentPorts(Buildable, Ports[0], Ports[1]))
	{
		return;
	}
	for (const ELiftFacing Facing : {ELiftFacing::Up, ELiftFacing::Down})
	{
		if (bOpenOnly && Ports[Facing == ELiftFacing::Up ? 0 : 1]->IsConnected())
		{
			continue;
		}
		OutEndpoints.Add({Buildable, ELiftEndpointKind::AttachmentPort, Facing, GetAttachmentPortName(Facing), Preview});
	}
}

bool FLiftEndpoint::HasBuildable() const
{
	return IsValid(Buildable);
}

bool FLiftEndpoint::IsOpen() const
{
	if (Kind == ELiftEndpointKind::FloorHole)
	{
		AFGBuildablePassthrough* Hole = GetFloorHole();
		return LiftAutoConnect::IsConveyorFloorHole(Hole) && !IsValid(LiftAutoConnect::GetFloorHoleSnapped(Hole, Facing));
	}
	UFGFactoryConnectionComponent* Connection = GetPort();
	return IsValid(Connection) && Connection->IsConnectionVertical() && !Connection->IsConnected();
}

FTransform FLiftEndpoint::GetWorldTransform() const
{
	const FTransform BuildableTransform = LiftAutoConnect::GetBuildableTransform(Buildable, Preview);
	UFGFactoryConnectionComponent* Connection = GetPort();
	if (Kind == ELiftEndpointKind::FloorHole || !IsValid(Connection))
	{
		return BuildableTransform;
	}
	return Connection->GetComponentTransform().GetRelativeTransform(Buildable->GetActorTransform()) * BuildableTransform;
}

FVector FLiftEndpoint::GetLocation() const
{
	return GetWorldTransform().GetLocation();
}

FVector FLiftEndpoint::GetOutwardNormal() const
{
	if (Kind == ELiftEndpointKind::FloorHole)
	{
		return Facing == ELiftFacing::Up ? FVector::UpVector : FVector::DownVector;
	}
	return GetWorldTransform().GetRotation().GetForwardVector().GetSafeNormal();
}

AFGBuildablePassthrough* FLiftEndpoint::GetFloorHole() const
{
	return Kind == ELiftEndpointKind::FloorHole ? Cast<AFGBuildablePassthrough>(Buildable) : nullptr;
}

UFGFactoryConnectionComponent* FLiftEndpoint::GetPort() const
{
	return Kind == ELiftEndpointKind::AttachmentPort ? FindFactoryConnection(Buildable, Port) : nullptr;
}

UFGFactoryConnectionComponent* FLiftEndpoint::GetContinuation() const
{
	if (Kind == ELiftEndpointKind::AttachmentPort)
	{
		return GetPort();
	}
	return LiftAutoConnect::GetFloorHoleSnapped(GetFloorHole(), LiftAutoConnect::Opposite(Facing));
}

UFGFactoryConnectionComponent* FLiftEndpoint::GetRepresentationConnection() const
{
	UFGFactoryConnectionComponent* Physical = GetContinuation();
	if (!IsValid(Physical) || !IsValid(Preview))
	{
		return Physical;
	}

	UFGFactoryConnectionComponent* AnyDuplicate = nullptr;
	for (const auto& Pair : Preview->mDuplicateConnectionToOriginalMap)
	{
		if (Pair.Value.Get() != Physical)
		{
			continue;
		}
		if (UFGFactoryConnectionComponent* Duplicate = Cast<UFGFactoryConnectionComponent>(Pair.Key.Get()))
		{
			if (Preview->mConnectionRepresentationMeshes.Contains(Duplicate))
			{
				return Duplicate;
			}
			AnyDuplicate = Duplicate;
		}
	}
	return IsValid(AnyDuplicate) ? AnyDuplicate : Physical;
}

AFGBuildableConveyorLift* FLiftEndpoint::GetAdjacentLift() const
{
	if (!IsValid(Buildable))
	{
		return nullptr;
	}
	if (Kind == ELiftEndpointKind::FloorHole)
	{
		UFGFactoryConnectionComponent* Continuation = GetContinuation();
		return IsValid(Continuation) ? Cast<AFGBuildableConveyorLift>(Continuation->GetOwner()) : nullptr;
	}

	UFGFactoryConnectionComponent* ThisPort = GetPort();
	TInlineComponentArray<UFGFactoryConnectionComponent*> Connections;
	Buildable->GetComponents(Connections);
	for (UFGFactoryConnectionComponent* Connection : Connections)
	{
		if (!IsValid(Connection) || Connection == ThisPort || !Connection->IsConnectionVertical())
		{
			continue;
		}
		UFGFactoryConnectionComponent* Peer = Connection->GetConnection();
		if (AFGBuildableConveyorLift* Lift = IsValid(Peer) ? Cast<AFGBuildableConveyorLift>(Peer->GetOwner()) : nullptr)
		{
			return Lift;
		}
	}
	return nullptr;
}

TSubclassOf<UFGRecipe> FLiftEndpoint::GetAdjacentLiftRecipe() const
{
	AFGBuildableConveyorLift* Lift = GetAdjacentLift();
	return IsValid(Lift) ? Lift->GetBuiltWithRecipe() : nullptr;
}

FVector FLiftEndpoint::GetAdjacentLiftHeading() const
{
	AFGBuildableConveyorLift* Lift = GetAdjacentLift();
	if (!IsValid(Lift))
	{
		return FVector::ZeroVector;
	}
	FTransform LiftTransform = Lift->GetActorTransform();
	if (IsValid(Preview))
	{
		const TObjectPtr<USceneComponent>* Root = Preview->mBuildableToNewRoot.Find(Lift);
		if (!Root || !IsValid(Root->Get()))
		{
			return FVector::ZeroVector;
		}
		LiftTransform = Root->Get()->GetComponentTransform();
	}
	FVector Heading = LiftTransform.GetRotation().GetForwardVector();
	Heading.Z = 0.0f;
	return Heading.GetSafeNormal();
}

ELiftFlow FLiftEndpoint::GetImpliedFlow() const
{
	UFGFactoryConnectionComponent* Continuation = GetContinuation();
	if (!IsValid(Continuation) || !LiftAutoConnect::IsTransportDirection(Continuation->GetDirection()))
	{
		return ELiftFlow::Unknown;
	}
	// Direction the lift's lower end needs: at an upward-facing endpoint the lift's lower end meets the continuation.
	const EFactoryConnectionDirection LowerEnd = Facing == ELiftFacing::Up
		? LiftAutoConnect::Opposite(Continuation->GetDirection())
		: Continuation->GetDirection();
	return LowerEnd == EFactoryConnectionDirection::FCD_INPUT ? ELiftFlow::Up : ELiftFlow::Down;
}

FLiftEndpoint FLiftEndpoint::InWorld(AFGBuildable* Constructed) const
{
	FLiftEndpoint Result = *this;
	Result.Buildable = Constructed;
	Result.Preview = nullptr;
	return Result;
}

FString FLiftEndpoint::Describe() const
{
	return FString::Printf(TEXT("%s[%s,%s%s%s]"),
		*GetNameSafe(Buildable),
		Kind == ELiftEndpointKind::FloorHole ? TEXT("floor-hole") : TEXT("port"),
		Facing == ELiftFacing::Up ? TEXT("up") : TEXT("down"),
		Port.IsNone() ? TEXT("") : TEXT(","),
		Port.IsNone() ? TEXT("") : *Port.ToString());
}
