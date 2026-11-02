#include "BlueprintVerticalConveyorConnectionManager.h"

#include "VerticalConveyorAutoConnect.h"
#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "FGBuildableSubsystem.h"
#include "FGConstructDisqualifier.h"
#include "FGFactoryConnectionComponent.h"
#include "FGRecipe.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorAttachmentHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "TimerManager.h"

FBlueprintVerticalConveyorConnectionManager::
	FBlueprintVerticalConveyorConnectionManager(
		AFGBlueprintHologram* blueprintHologram)
	: FGBlueprintOpenConnectionManagerBase(blueprintHologram)
{
}

bool FBlueprintVerticalConveyorConnectionManager::IsConveyorFloorHole(
	const AFGBuildablePassthrough* hole)
{
	if (!IsValid(hole))
	{
		return false;
	}

	const TSubclassOf<UFGConnectionComponent> connectionClass =
		hole->GetConnectionClass();
	return connectionClass &&
		connectionClass->IsChildOf(UFGFactoryConnectionComponent::StaticClass());
}

bool FBlueprintVerticalConveyorConnectionManager::TryGetAttachmentLiftPorts(
	AFGBuildable* buildable,
	FAttachmentLiftPorts& outPorts)
{
	outPorts = {};

	// Capability contract:
	// - must participate in vanilla conveyor-attachment state/lifecycle semantics;
	// - must expose BOTH canonical lift-port identities used by
	//   AFGConveyorAttachmentHologram;
	// - both canonical components must actually be vertical factory connections.
	//
	// This intentionally avoids an allow-list of splitter/merger class families.
	// Vanilla subclasses and modded variants that preserve the standard attachment
	// lift contract become compatible automatically, while arbitrary factory
	// buildables and non-standard conveyor attachments remain excluded.
	if (!IsValid(Cast<AFGBuildableConveyorAttachment>(buildable)))
	{
		return false;
	}

	outPorts.Bottom = FindFactoryConnection(
		buildable,
		AFGConveyorAttachmentHologram::mLiftConnection_Bottom);
	outPorts.Top = FindFactoryConnection(
		buildable,
		AFGConveyorAttachmentHologram::mLiftConnection_Top);

	if (!IsValid(outPorts.Bottom) ||
		!IsValid(outPorts.Top) ||
		outPorts.Bottom == outPorts.Top ||
		!outPorts.Bottom->IsConnectionVertical() ||
		!outPorts.Top->IsConnectionVertical())
	{
		outPorts = {};
		return false;
	}

	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::IsSupportedAttachment(
	const AFGBuildable* buildable)
{
	FAttachmentLiftPorts ports;
	return IsValid(buildable) &&
		TryGetAttachmentLiftPorts(
			const_cast<AFGBuildable*>(buildable),
			ports);
}

bool FBlueprintVerticalConveyorConnectionManager::IsSupportedBuildable(
	const AFGBuildable* buildable)
{
	return IsConveyorFloorHole(Cast<AFGBuildablePassthrough>(buildable)) ||
		IsSupportedAttachment(buildable);
}

EBlueprintVerticalEndpointSide
FBlueprintVerticalConveyorConnectionManager::OppositeSide(
	EBlueprintVerticalEndpointSide side)
{
	return side == EBlueprintVerticalEndpointSide::Top
		? EBlueprintVerticalEndpointSide::Bottom
		: EBlueprintVerticalEndpointSide::Top;
}

EFactoryConnectionDirection
FBlueprintVerticalConveyorConnectionManager::OppositeDirection(
	EFactoryConnectionDirection direction)
{
	switch (direction)
	{
	case EFactoryConnectionDirection::FCD_INPUT:
		return EFactoryConnectionDirection::FCD_OUTPUT;
	case EFactoryConnectionDirection::FCD_OUTPUT:
		return EFactoryConnectionDirection::FCD_INPUT;
	default:
		return EFactoryConnectionDirection::FCD_ANY;
	}
}

bool FBlueprintVerticalConveyorConnectionManager::
	ResolveDirectConnectionDirections(
		const UFGFactoryConnectionComponent* blueprintConnection,
		const UFGFactoryConnectionComponent* targetConnection,
		EFactoryConnectionDirection& outBlueprintDirection,
		EFactoryConnectionDirection& outTargetDirection)
{
	outBlueprintDirection = EFactoryConnectionDirection::FCD_ANY;
	outTargetDirection = EFactoryConnectionDirection::FCD_ANY;
	if (!IsValid(blueprintConnection) || !IsValid(targetConnection))
	{
		return false;
	}

	const auto isTransportDirection = [](EFactoryConnectionDirection direction)
	{
		return direction == EFactoryConnectionDirection::FCD_INPUT ||
			direction == EFactoryConnectionDirection::FCD_OUTPUT;
	};

	outBlueprintDirection = blueprintConnection->GetDirection();
	outTargetDirection = targetConnection->GetDirection();
	if (!isTransportDirection(outBlueprintDirection) &&
		!isTransportDirection(outTargetDirection))
	{
		return false;
	}
	if (!isTransportDirection(outBlueprintDirection))
	{
		outBlueprintDirection = OppositeDirection(outTargetDirection);
	}
	if (!isTransportDirection(outTargetDirection))
	{
		outTargetDirection = OppositeDirection(outBlueprintDirection);
	}

	return OppositeDirection(outBlueprintDirection) == outTargetDirection;
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::GetFloorHoleSnappedConnection(
	const AFGBuildablePassthrough* hole,
	EBlueprintVerticalEndpointSide side)
{
	if (!IsValid(hole))
	{
		return nullptr;
	}

	AFGBuildablePassthrough* mutableHole =
		const_cast<AFGBuildablePassthrough*>(hole);
	return side == EBlueprintVerticalEndpointSide::Top
		? mutableHole->GetTopSnappedConnection<UFGFactoryConnectionComponent>()
		: mutableHole->GetBottomSnappedConnection<UFGFactoryConnectionComponent>();
}

void FBlueprintVerticalConveyorConnectionManager::SetFloorHoleSnappedConnection(
	AFGBuildablePassthrough* hole,
	EBlueprintVerticalEndpointSide side,
	UFGFactoryConnectionComponent* connection)
{
	if (!IsValid(hole))
	{
		return;
	}

	if (side == EBlueprintVerticalEndpointSide::Top)
	{
		hole->SetTopSnappedConnection(connection);
	}
	else
	{
		hole->SetBottomSnappedConnection(connection);
	}
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::FindFactoryConnection(
	AFGBuildable* buildable,
	FName connectionName)
{
	if (!IsValid(buildable) || connectionName.IsNone())
	{
		return nullptr;
	}

	TInlineComponentArray<UFGFactoryConnectionComponent*> connections;
	buildable->GetComponents(connections);
	for (UFGFactoryConnectionComponent* connection : connections)
	{
		if (IsValid(connection) && connection->GetFName() == connectionName)
		{
			return connection;
		}
	}
	return nullptr;
}

void FBlueprintVerticalConveyorConnectionManager::GatherEndpoints(
	AFGBuildable* buildable,
	bool useBlueprintPreviewTransform,
	bool requireOpen,
	TArray<FEndpointRef>& outEndpoints) const
{
	if (!IsValid(buildable))
	{
		return;
	}

	if (AFGBuildablePassthrough* hole = Cast<AFGBuildablePassthrough>(buildable))
	{
		if (!IsConveyorFloorHole(hole))
		{
			return;
		}

		for (const EBlueprintVerticalEndpointSide side :
			{EBlueprintVerticalEndpointSide::Bottom,
			 EBlueprintVerticalEndpointSide::Top})
		{
			if (requireOpen &&
				IsValid(GetFloorHoleSnappedConnection(hole, side)))
			{
				continue;
			}

			FEndpointRef& endpoint = outEndpoints.AddDefaulted_GetRef();
			endpoint.Buildable = buildable;
			endpoint.Kind = EBlueprintVerticalEndpointKind::FloorHoleSide;
			endpoint.Side = side;
			endpoint.UsesBlueprintPreviewTransform = useBlueprintPreviewTransform;
		}
		return;
	}

	FAttachmentLiftPorts attachmentPorts;
	if (!TryGetAttachmentLiftPorts(buildable, attachmentPorts))
	{
		return;
	}

	// The capability probe above owns the definition of a vanilla-compatible
	// vertical conveyor attachment. Enumerate exactly those validated components
	// here so support detection and endpoint discovery cannot drift apart.
	//
	// Both ports are gathered because blueprint initialization does not yet know
	// the eventual opposite endpoint. Candidate matching later selects the port
	// vanilla actually uses for that span (TopConnection when the floor hole is
	// above the attachment, BottomConnection when it is below).
	struct FLiftPortSpec
	{
		EBlueprintVerticalEndpointSide Side;
		FName Name;
		UFGFactoryConnectionComponent* Connection;
	};
	const FLiftPortSpec liftPorts[] =
	{
		{EBlueprintVerticalEndpointSide::Bottom,
		 AFGConveyorAttachmentHologram::mLiftConnection_Bottom,
		 attachmentPorts.Bottom},
		{EBlueprintVerticalEndpointSide::Top,
		 AFGConveyorAttachmentHologram::mLiftConnection_Top,
		 attachmentPorts.Top}
	};
	for (const FLiftPortSpec& port : liftPorts)
	{
		UFGFactoryConnectionComponent* connection = port.Connection;
		if (requireOpen && connection->IsConnected())
		{
			continue;
		}

		FEndpointRef& endpoint = outEndpoints.AddDefaulted_GetRef();
		endpoint.Buildable = buildable;
		endpoint.Kind = EBlueprintVerticalEndpointKind::AttachmentPort;
		endpoint.Side = port.Side;
		endpoint.ConnectionName = port.Name;
		endpoint.UsesBlueprintPreviewTransform = useBlueprintPreviewTransform;
	}
}

void FBlueprintVerticalConveyorConnectionManager::GatherDiscoveryCandidates(
	TArray<AFGBuildable*>& outBuildables) const
{
	outBuildables.Reset();

	TSet<AFGBuildable*> uniqueBuildables;
	auto addCandidate = [&outBuildables, &uniqueBuildables](
		AFGBuildable* buildable)
	{
		if (IsSupportedBuildable(buildable) &&
			!uniqueBuildables.Contains(buildable))
		{
			uniqueBuildables.Add(buildable);
			outBuildables.Add(buildable);
		}
	};

	// Preserve vanilla's ordinary overlap feed. The complete-column query below
	// supplements it; it does not replace the parent hologram lifecycle.
	for (const TWeakObjectPtr<AFGBuildable>& weakBuildable : NearbyBuildables)
	{
		addCandidate(weakBuildable.Get());
	}

	AFGBuildableSubsystem* buildableSubsystem =
		AFGBuildableSubsystem::Get(GetHologram());
	if (!IsValid(buildableSubsystem))
	{
		return;
	}

	FBox sourceBounds(ForceInit);
	bool hasSource = false;
	for (const FConnectionState& state : ConnectionStates)
	{
		const FEndpointRef endpoint = GetBlueprintEndpoint(state, false);
		if (!IsValid(endpoint.Buildable))
		{
			continue;
		}
		sourceBounds += GetEndpointWorldTransform(endpoint).GetLocation();
		hasSource = true;
	}
	if (!hasSource)
	{
		return;
	}

	// The parent clearance detector is only vanilla's actor-discovery broadphase;
	// it is not a semantic connection-distance rule. Query the world-height prism
	// through all source endpoint columns, then let exact XY matching, no-tunneling
	// rules, and the real Conveyor Lift hologram decide which candidates are valid.
	// WORLD_MAX is Unreal's world-coordinate bound, not a mod-defined lift range.
	FBox queryBounds = sourceBounds;
	queryBounds.Min.Z = -WORLD_MAX;
	queryBounds.Max.Z = WORLD_MAX;
	TArray<AFGBuildable*> spatialCandidates;
	buildableSubsystem->GetCollidingBuildablesInBoundingBox(
		spatialCandidates,
		queryBounds);
	for (AFGBuildable* buildable : spatialCandidates)
	{
		addCandidate(buildable);
	}
}

bool FBlueprintVerticalConveyorConnectionManager::IsEndpointOpen(
	const FEndpointRef& endpoint) const
{
	if (!IsValid(endpoint.Buildable))
	{
		return false;
	}

	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		const AFGBuildablePassthrough* hole =
			Cast<AFGBuildablePassthrough>(endpoint.Buildable);
		return IsConveyorFloorHole(hole) &&
			!IsValid(GetFloorHoleSnappedConnection(hole, endpoint.Side));
	}

	UFGFactoryConnectionComponent* connection =
		FindFactoryConnection(endpoint.Buildable, endpoint.ConnectionName);
	return IsValid(connection) &&
		connection->IsConnectionVertical() &&
		!connection->IsConnected();
}

bool FBlueprintVerticalConveyorConnectionManager::HasInterveningBlueprintEndpoint(
	const FEndpointRef& sourceEndpoint,
	const FEndpointRef& targetEndpoint,
	FEndpointRef* outBlockingEndpoint) const
{
	const FVector sourceLocation =
		GetEndpointWorldTransform(sourceEndpoint).GetLocation();
	const FVector targetLocation =
		GetEndpointWorldTransform(targetEndpoint).GetLocation();

	const float minZ = FMath::Min(sourceLocation.Z, targetLocation.Z);
	const float maxZ = FMath::Max(sourceLocation.Z, targetLocation.Z);
	if (!(minZ < maxZ))
	{
		return false;
	}

	for (const FEndpointRef& blocker : BlueprintPhysicalEndpoints)
	{
		if (!IsValid(blocker.Buildable) ||
			blocker.Buildable == sourceEndpoint.Buildable)
		{
			// The source buildable is the boundary origin, not an intervening
			// obstacle. This also excludes sibling endpoints on the same floor hole
			// or attachment. Identity must be explicit here: Blueprint preview
			// transforms may move between reads, so coordinate equality is not a
			// reliable way to prevent a source from blocking itself.
			continue;
		}

		const FVector blockerLocation =
			GetEndpointWorldTransform(blocker).GetLocation();

		// Same-column means semantic equality in X/Y. Use Unreal's numerical
		// equality primitive; do not introduce a mod-defined spatial tolerance.
		if (!FMath::IsNearlyZero(blockerLocation.X - sourceLocation.X) ||
			!FMath::IsNearlyZero(blockerLocation.Y - sourceLocation.Y))
		{
			continue;
		}

		// Only a STRICTLY intervening endpoint blocks. The source's opposite side
		// and sibling attachment port occupy the same Z plane and therefore do not
		// self-block.
		if (blockerLocation.Z > minZ && blockerLocation.Z < maxZ)
		{
			if (outBlockingEndpoint)
			{
				*outBlockingEndpoint = blocker;
			}
			return true;
		}
	}

	return false;
}

FBlueprintVerticalConveyorConnectionManager::FEndpointRef
FBlueprintVerticalConveyorConnectionManager::GetBlueprintEndpoint(
	const FConnectionState& state,
	bool useConstructedBlueprintBuildable) const
{
	FEndpointRef endpoint;
	endpoint.Buildable = useConstructedBlueprintBuildable
		? state.ConstructedBlueprintBuildable
		: state.BlueprintBuildable;
	endpoint.Kind = state.BlueprintKind;
	endpoint.Side = state.BlueprintSide;
	endpoint.ConnectionName = state.BlueprintConnectionName;
	endpoint.UsesBlueprintPreviewTransform = !useConstructedBlueprintBuildable;
	return endpoint;
}

FBlueprintVerticalConveyorConnectionManager::FEndpointRef
FBlueprintVerticalConveyorConnectionManager::GetTargetEndpoint(
	const FConnectionState& state) const
{
	FEndpointRef endpoint;
	endpoint.Buildable = state.TargetBuildable;
	endpoint.Kind = state.TargetKind;
	endpoint.Side = state.TargetSide;
	endpoint.ConnectionName = state.TargetConnectionName;
	endpoint.UsesBlueprintPreviewTransform = false;
	return endpoint;
}

FBlueprintVerticalConveyorConnectionManager::FEndpointKey
FBlueprintVerticalConveyorConnectionManager::MakeEndpointKey(
	const FEndpointRef& endpoint) const
{
	// A splitter/merger lift port has a stable component identity, but its
	// effective top/bottom facing is determined by which side the other endpoint
	// lies on. Do not let that derived facing side create two claim identities for
	// the same physical attachment connection.
	const EBlueprintVerticalEndpointSide keySide =
		endpoint.Kind == EBlueprintVerticalEndpointKind::AttachmentPort
			? EBlueprintVerticalEndpointSide::Top
			: endpoint.Side;
	return {
		endpoint.Buildable,
		endpoint.Kind,
		keySide,
		endpoint.ConnectionName};
}

FString FBlueprintVerticalConveyorConnectionManager::DescribeEndpoint(
	const FEndpointRef& endpoint) const
{
	const TCHAR* kind = endpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide
		? TEXT("floor-hole")
		: TEXT("attachment-port");
	const TCHAR* side = endpoint.Side == EBlueprintVerticalEndpointSide::Top
		? TEXT("top")
		: TEXT("bottom");
	return FString::Printf(
		TEXT("%s[%s,%s,%s]"),
		*GetNameSafe(endpoint.Buildable),
		kind,
		side,
		*endpoint.ConnectionName.ToString());
}

void FBlueprintVerticalConveyorConnectionManager::RegisterNearbyActor(AActor* actor)
{
	AFGBuildable* buildable = Cast<AFGBuildable>(actor);
	if (!IsSupportedBuildable(buildable))
	{
		return;
	}

	const bool alreadyRegistered = NearbyBuildables.ContainsByPredicate(
		[buildable](const TWeakObjectPtr<AFGBuildable>& candidate)
		{
			return candidate.Get() == buildable;
		});
	if (!alreadyRegistered)
	{
		NearbyBuildables.Add(buildable);
	}
}

void FBlueprintVerticalConveyorConnectionManager::UnregisterNearbyActor(AActor* actor)
{
	NearbyBuildables.RemoveAllSwap(
		[actor](const TWeakObjectPtr<AFGBuildable>& candidate)
		{
			return candidate.Get() == actor;
		});
}

void FBlueprintVerticalConveyorConnectionManager::Initialize(
	TArray<AFGBuildable*> buildables)
{
	const double startSeconds = FPlatformTime::Seconds();

	// Child holograms are registered by name on the parent hologram. Destroy() is
	// deferred and does not immediately remove that registration, so destroying a
	// child and spawning another child with the same name in the same placement
	// session can trip AFGHologram::AddChild's duplicate-name assertion. Keep old
	// bridge holograms disabled instead; SpawnBridgeHologram reuses the child that
	// belongs to the selected state/recipe.
	for (FConnectionState& state : ConnectionStates)
	{
		DisableBridge(state);
	}
	ConnectionStates.Reset();
	BlueprintPhysicalEndpoints.Reset();
	LoggedSourceTunnelStates.Reset();

	for (int32 buildableIndex = 0; buildableIndex < buildables.Num(); ++buildableIndex)
	{
		AFGBuildable* buildable = buildables[buildableIndex];

		TArray<FEndpointRef> physicalEndpoints;
		GatherEndpoints(buildable, true, false, physicalEndpoints);
		BlueprintPhysicalEndpoints.Append(physicalEndpoints);

		TArray<FEndpointRef> endpoints;
		GatherEndpoints(buildable, true, true, endpoints);

		for (const FEndpointRef& endpoint : endpoints)
		{
			FConnectionState& state = ConnectionStates.AddDefaulted_GetRef();
			state.BlueprintBuildable = buildable;
			state.BlueprintBuildableIndex = buildableIndex;
			state.BlueprintKind = endpoint.Kind;
			state.BlueprintSide = endpoint.Side;
			state.BlueprintConnectionName = endpoint.ConnectionName;

			if (endpoint.Kind ==
				EBlueprintVerticalEndpointKind::FloorHoleSide)
			{
				UFGFactoryConnectionComponent* continuation =
					GetTransportConnectionAcrossEndpoint(endpoint);
				state.BlueprintContinuationWasPresent = continuation != nullptr;
				if (IsValid(continuation))
				{
					AFGBuildableConveyorLift* continuationOwner =
						Cast<AFGBuildableConveyorLift>(
							continuation->GetOwner());
					state.BlueprintContinuationBuildableIndex =
						buildables.IndexOfByKey(continuationOwner);
					if (IsValid(continuationOwner))
					{
						if (continuation == continuationOwner->GetConnection0())
						{
							state.BlueprintContinuationConnectionIndex = 0;
						}
						else if (continuation ==
							continuationOwner->GetConnection1())
						{
							state.BlueprintContinuationConnectionIndex = 1;
						}
					}

					if (state.BlueprintContinuationBuildableIndex == INDEX_NONE ||
						state.BlueprintContinuationConnectionIndex ==
							INDEX_NONE)
					{
						UE_LOG(
							LogVerticalConveyorAutoConnect,
							Warning,
							TEXT("VerticalConveyorAutoConnect: source continuation cannot be mapped as a blueprint lift component endpoint=%s continuation=%s owner=%s"),
							*DescribeEndpoint(endpoint),
							*GetNameSafe(continuation),
							*GetNameSafe(continuationOwner));
					}
				}
			}

			// The bridge hologram is spawned on demand by EnsureBridgeRecipe once
			// the state has a target. Spawning one per open endpoint here cost
			// several milliseconds each, on every new blueprint hologram, including
			// the one created right after each placement.
			state.LiftRecipe = GetEndpointLiftRecipe(endpoint);
		}
	}

	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: initialized %d open vertical endpoints in %.1f ms"),
		ConnectionStates.Num(),
		(FPlatformTime::Seconds() - startSeconds) * 1000.0);
}

bool FBlueprintVerticalConveyorConnectionManager::
	TryGetBlueprintBuildableWorldTransform(
		const AFGBuildable* buildable,
		FTransform& outTransform) const
{
	AFGBlueprintHologram* hologram = GetHologram();
	if (IsValid(hologram) && IsValid(buildable))
	{
		const TObjectPtr<USceneComponent>* visualRoot =
			hologram->mBuildableToNewRoot.Find(
				const_cast<AFGBuildable*>(buildable));
		if (visualRoot && IsValid(visualRoot->Get()))
		{
			outTransform = visualRoot->Get()->GetComponentTransform();
			return true;
		}
	}

	outTransform = FTransform::Identity;
	return false;
}

FTransform FBlueprintVerticalConveyorConnectionManager::GetBuildableWorldTransform(
	const FEndpointRef& endpoint) const
{
	if (!IsValid(endpoint.Buildable))
	{
		return FTransform::Identity;
	}

	if (endpoint.UsesBlueprintPreviewTransform)
	{
		FTransform visualTransform;
		if (TryGetBlueprintBuildableWorldTransform(
			endpoint.Buildable,
			visualTransform))
		{
			return visualTransform;
		}
	}

	return endpoint.Buildable->GetActorTransform();
}

FTransform FBlueprintVerticalConveyorConnectionManager::GetEndpointWorldTransform(
	const FEndpointRef& endpoint) const
{
	const FTransform buildableWorld = GetBuildableWorldTransform(endpoint);
	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		return buildableWorld;
	}

	UFGFactoryConnectionComponent* connection =
		FindFactoryConnection(endpoint.Buildable, endpoint.ConnectionName);
	if (!IsValid(connection) || !IsValid(endpoint.Buildable))
	{
		return buildableWorld;
	}

	const FTransform relativeTransform =
		connection->GetComponentTransform().GetRelativeTransform(
			endpoint.Buildable->GetActorTransform());
	return relativeTransform * buildableWorld;
}

FVector FBlueprintVerticalConveyorConnectionManager::
	GetEndpointOutwardNormalWorld(const FEndpointRef& endpoint) const
{
	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		return endpoint.Side == EBlueprintVerticalEndpointSide::Top
			? FVector::UpVector
			: FVector::DownVector;
	}

	return GetEndpointWorldTransform(endpoint)
		.GetRotation()
		.GetForwardVector()
		.GetSafeNormal();
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::GetDirectEndpointConnection(
	const FEndpointRef& endpoint) const
{
	return endpoint.Kind == EBlueprintVerticalEndpointKind::AttachmentPort
		? FindFactoryConnection(endpoint.Buildable, endpoint.ConnectionName)
		: nullptr;
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::GetTransportConnectionAcrossEndpoint(
	const FEndpointRef& endpoint) const
{
	if (endpoint.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		return GetDirectEndpointConnection(endpoint);
	}

	const AFGBuildablePassthrough* hole =
		Cast<AFGBuildablePassthrough>(endpoint.Buildable);
	return GetFloorHoleSnappedConnection(hole, OppositeSide(endpoint.Side));
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::
	GetBlueprintRepresentationConnection(const FEndpointRef& endpoint) const
{
	UFGFactoryConnectionComponent* physicalConnection =
		GetTransportConnectionAcrossEndpoint(endpoint);
	AFGBlueprintHologram* hologram = GetHologram();
	if (!IsValid(physicalConnection) || !IsValid(hologram))
	{
		return physicalConnection;
	}

	// Vanilla represents blueprint-world connections with duplicated components
	// attached to the placed hologram. Its state-change delegate receives those
	// preview components, not necessarily the original BlueprintWorld component.
	// Reuse the duplicate already created by AFGBlueprintHologram so the standard
	// automatic-link icon can replace the correct direction indicator.
	UFGFactoryConnectionComponent* fallbackDuplicate = nullptr;
	for (const auto& pair : hologram->mDuplicateConnectionToOriginalMap)
	{
		if (pair.Value.Get() == physicalConnection)
		{
			if (UFGFactoryConnectionComponent* duplicate =
				Cast<UFGFactoryConnectionComponent>(pair.Key.Get()))
			{
				if (hologram->mConnectionRepresentationMeshes.Contains(duplicate))
				{
					return duplicate;
				}
				fallbackDuplicate = duplicate;
			}
		}
	}
	return IsValid(fallbackDuplicate)
		? fallbackDuplicate
		: physicalConnection;
}

AFGBuildableConveyorLift*
FBlueprintVerticalConveyorConnectionManager::GetAdjacentLift(
	const FEndpointRef& endpoint) const
{
	if (!IsValid(endpoint.Buildable))
	{
		return nullptr;
	}

	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		UFGFactoryConnectionComponent* connection =
			GetTransportConnectionAcrossEndpoint(endpoint);
		return IsValid(connection)
			? Cast<AFGBuildableConveyorLift>(connection->GetOwner())
			: nullptr;
	}

	UFGFactoryConnectionComponent* openConnection =
		GetDirectEndpointConnection(endpoint);
	TInlineComponentArray<UFGFactoryConnectionComponent*> connections;
	endpoint.Buildable->GetComponents(connections);
	for (UFGFactoryConnectionComponent* connection : connections)
	{
		if (!IsValid(connection) ||
			connection == openConnection ||
			!connection->IsConnectionVertical())
		{
			continue;
		}

		UFGFactoryConnectionComponent* connected = connection->GetConnection();
		if (AFGBuildableConveyorLift* lift = IsValid(connected)
			? Cast<AFGBuildableConveyorLift>(connected->GetOwner())
			: nullptr)
		{
			return lift;
		}
	}
	return nullptr;
}

TSubclassOf<UFGRecipe>
FBlueprintVerticalConveyorConnectionManager::GetEndpointLiftRecipe(
	const FEndpointRef& endpoint) const
{
	if (AFGBuildableConveyorLift* lift = GetAdjacentLift(endpoint))
	{
		return lift->GetBuiltWithRecipe();
	}
	return nullptr;
}

EFactoryConnectionDirection
FBlueprintVerticalConveyorConnectionManager::GetRequiredBridgeDirection(
	const FEndpointRef& endpoint) const
{
	UFGFactoryConnectionComponent* outside =
		GetTransportConnectionAcrossEndpoint(endpoint);
	if (!IsValid(outside))
	{
		return EFactoryConnectionDirection::FCD_ANY;
	}

	// Return a normalized contribution to the direction of the PHYSICAL LOWER
	// bridge endpoint, not the direction of this endpoint itself.
	//
	// This deliberately preserves the old floor-hole-only ResolveBaseDirection
	// semantics:
	//   lower/open-top endpoint    -> Opposite(outside)
	//   upper/open-bottom endpoint -> outside
	//
	// The same side rule also applies to a direct lift-attachment port:
	// an output top port requires an input lower bridge endpoint, while an input
	// bottom port likewise implies an input lower bridge endpoint.
	return endpoint.Side == EBlueprintVerticalEndpointSide::Top
		? OppositeDirection(outside->GetDirection())
		: outside->GetDirection();
}


bool FBlueprintVerticalConveyorConnectionManager::
	RestorePersistedAttachmentDirectionBeforeConstruct(
		const FEndpointRef& attachmentEndpoint,
		EFactoryConnectionDirection expectedDirection) const
{
	if (attachmentEndpoint.Kind !=
			EBlueprintVerticalEndpointKind::AttachmentPort ||
		(expectedDirection != EFactoryConnectionDirection::FCD_INPUT &&
		 expectedDirection != EFactoryConnectionDirection::FCD_OUTPUT))
	{
		return false;
	}

	AFGBuildableConveyorAttachment* attachment =
		Cast<AFGBuildableConveyorAttachment>(attachmentEndpoint.Buildable);
	UFGFactoryConnectionComponent* selectedConnection =
		GetDirectEndpointConnection(attachmentEndpoint);
	if (!IsValid(attachment) || !IsValid(selectedConnection))
	{
		return false;
	}

	const EFactoryConnectionDirection runtimeDirection =
		selectedConnection->GetDirection();

	// Conveyor attachments persist directions so BeginPlay can restore dynamic
	// input/output roles after spawning/loading. Before BeginPlay the component
	// can be ANY *or a concrete default/stale value*, so the persisted state is
	// authoritative for a freshly constructed blueprint attachment.
	TInlineComponentArray<UFGFactoryConnectionComponent*> orderedConnections;
	attachment->GetComponents(orderedConnections);
	UFGFactoryConnectionComponent::SortComponentList(orderedConnections);

	const int32 connectionIndex =
		orderedConnections.IndexOfByKey(selectedConnection);
	if (orderedConnections.Num() != attachment->mSavedDirections.Num() ||
		connectionIndex == INDEX_NONE ||
		!attachment->mSavedDirections.IsValidIndex(connectionIndex))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: cannot restore persisted attachment direction actor=%s port=%s index=%d componentCount=%d savedCount=%d"),
			*GetNameSafe(attachment),
			*GetNameSafe(selectedConnection),
			connectionIndex,
			orderedConnections.Num(),
			attachment->mSavedDirections.Num());
		return false;
	}

	const EFactoryConnectionDirection savedDirection =
		attachment->mSavedDirections[connectionIndex];
	if (savedDirection != expectedDirection)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: persisted attachment direction disagrees with locked transport actor=%s port=%s index=%d saved=%d expected=%d"),
			*GetNameSafe(attachment),
			*GetNameSafe(selectedConnection),
			connectionIndex,
			static_cast<int32>(savedDirection),
			static_cast<int32>(expectedDirection));
		return false;
	}

	if (runtimeDirection != savedDirection)
	{
		selectedConnection->SetDirection(savedDirection);
	}

	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::
	PrepareAttachmentEndpointDirection(
		const FEndpointRef& attachmentEndpoint,
		const FEndpointRef& blueprintEndpoint,
		bool isTransportInput,
		bool finalValidation,
		EFactoryConnectionDirection& outDirection) const
{
	if (attachmentEndpoint.Kind !=
		EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		return false;
	}

	// The attachment is outside the generated lift. It must therefore output
	// into the lift at the transport-input end and accept lift output at the
	// transport-output end.
	outDirection = isTransportInput
		? EFactoryConnectionDirection::FCD_OUTPUT
		: EFactoryConnectionDirection::FCD_INPUT;

	UFGFactoryConnectionComponent* connection =
		GetDirectEndpointConnection(attachmentEndpoint);
	if (!IsValid(connection))
	{
		return false;
	}

	// Final bridge construction precedes BeginPlay on blueprint-owned
	// attachments. Restore either end of an attachment-to-attachment bridge,
	// not merely placement slot 0.
	const bool originatedFromBlueprint =
		attachmentEndpoint.Buildable == blueprintEndpoint.Buildable;
	if (finalValidation && originatedFromBlueprint &&
		!RestorePersistedAttachmentDirectionBeforeConstruct(
			attachmentEndpoint,
			outDirection))
	{
		return false;
	}

	const EFactoryConnectionDirection runtimeDirection =
		connection->GetDirection();
	return (runtimeDirection != EFactoryConnectionDirection::FCD_INPUT &&
			runtimeDirection != EFactoryConnectionDirection::FCD_OUTPUT) ||
		runtimeDirection == outDirection;
}

bool FBlueprintVerticalConveyorConnectionManager::
	CanConnectLiftToPlacementEnd(
		AFGConveyorLiftHologram* bridge,
		const FEndpointRef& placementEnd) const
{
	if (!IsValid(bridge))
	{
		return false;
	}

	if (placementEnd.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		return IsConveyorFloorHole(
			Cast<AFGBuildablePassthrough>(placementEnd.Buildable));
	}

	UFGFactoryConnectionComponent* liftConnection =
		bridge->mConnectionComponents[1].Get();
	UFGFactoryConnectionComponent* directAttachmentConnection =
		GetDirectEndpointConnection(placementEnd);
	UFGFactoryConnectionComponent* attachmentConnection =
		placementEnd.UsesBlueprintPreviewTransform
			? GetBlueprintRepresentationConnection(placementEnd)
			: directAttachmentConnection;
	if (!IsValid(liftConnection) || !IsValid(attachmentConnection))
	{
		return false;
	}

	// This call is the runtime capability contract. Vanilla currently rejects a
	// lift whose second placement endpoint is a vertical attachment port. A mod
	// such as VerticalLogisticsQoL can extend this exact hologram method. Calling
	// the real method keeps compatibility implicit and fails closed when no such
	// capability is present.
	//
	// Blueprint preview buildables keep their real connection components in
	// blueprint-local space. Prefer the world-space duplicate that vanilla creates
	// for connection representation; hidden vertical attachment ports currently do
	// not receive one. In that fallback case, stage the BlueprintWorld component at
	// its resolved preview location for this read-only capability call and restore
	// its authored relative transform immediately afterwards. Making that transient
	// preview component movable once avoids re-registering it on every hologram tick.
	//
	// A synthetic lift hologram also does not move its second connection component
	// to mTopTransform, so stage and restore that transient component as well.
	const FTransform liftConnectionRelativeTransform =
		liftConnection->GetRelativeTransform();
	const FTransform placementEndWorldTransform =
		GetEndpointWorldTransform(placementEnd);
	const bool stageBlueprintAttachment =
		placementEnd.UsesBlueprintPreviewTransform &&
		attachmentConnection == directAttachmentConnection;
	const FTransform attachmentConnectionRelativeTransform =
		attachmentConnection->GetRelativeTransform();
	if (liftConnection->GetMobility() != EComponentMobility::Movable)
	{
		liftConnection->SetMobility(EComponentMobility::Movable);
	}
	if (stageBlueprintAttachment)
	{
		if (attachmentConnection->GetMobility() != EComponentMobility::Movable)
		{
			attachmentConnection->SetMobility(EComponentMobility::Movable);
		}
		attachmentConnection->SetWorldTransform(placementEndWorldTransform);
	}
	liftConnection->SetWorldLocation(placementEndWorldTransform.GetLocation());

	const bool canConnect = bridge->CanConnectToConnection(
		liftConnection,
		attachmentConnection);

	liftConnection->SetRelativeTransform(liftConnectionRelativeTransform);
	if (stageBlueprintAttachment)
	{
		attachmentConnection->SetRelativeTransform(
			attachmentConnectionRelativeTransform);
	}
	return canConnect;
}

bool FBlueprintVerticalConveyorConnectionManager::ResolveLiftPlacementEndpoints(
	const FEndpointRef& transportInput,
	const FEndpointRef& transportOutput,
	FEndpointRef& outPlacementStart,
	FEndpointRef& outPlacementEnd) const
{
	// Blueprint ownership is deliberately irrelevant here. Manual vanilla
	// placement establishes that an attachment<->floor-hole lift must START at
	// the splitter/merger side, even when that attachment is the world-side target
	// or the transport output. For two attachments, start at the transport-input
	// attachment and let the live lift hologram decide whether it can connect its
	// second endpoint. Floor-hole<->floor-hole keeps the known-working
	// transport-input-first convention.
	if (transportInput.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		outPlacementStart = transportInput;
		outPlacementEnd = transportOutput;
		return true;
	}
	if (transportOutput.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		outPlacementStart = transportOutput;
		outPlacementEnd = transportInput;
		return true;
	}

	outPlacementStart = transportInput;
	outPlacementEnd = transportOutput;
	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::ResolveLowerEndpointDirection(
	const FEndpointRef& first,
	const FEndpointRef& second,
	EFactoryConnectionDirection& outLowerEndpointDirection,
	FEndpointRef& outLower,
	FEndpointRef& outUpper) const
{
	const float firstZ = GetEndpointWorldTransform(first).GetLocation().Z;
	const float secondZ = GetEndpointWorldTransform(second).GetLocation().Z;
	if (firstZ <= secondZ)
	{
		outLower = first;
		outUpper = second;
	}
	else
	{
		outLower = second;
		outUpper = first;
	}

	// GetRequiredBridgeDirection() already normalizes BOTH physical endpoints to
	// the direction required at the lower bridge end. Compatible contributions
	// therefore have to be EQUAL. Treating the upper contribution as an actual
	// upper-end direction here was the generalized direction regression.
	const EFactoryConnectionDirection lowerRequired =
		GetRequiredBridgeDirection(outLower);
	const EFactoryConnectionDirection upperRequired =
		GetRequiredBridgeDirection(outUpper);

	const bool lowerConcrete =
		lowerRequired == EFactoryConnectionDirection::FCD_INPUT ||
		lowerRequired == EFactoryConnectionDirection::FCD_OUTPUT;
	const bool upperConcrete =
		upperRequired == EFactoryConnectionDirection::FCD_INPUT ||
		upperRequired == EFactoryConnectionDirection::FCD_OUTPUT;

	if (lowerConcrete && upperConcrete &&
		upperRequired != lowerRequired)
	{
		return false;
	}

	if (lowerConcrete)
	{
		outLowerEndpointDirection = lowerRequired;
		return true;
	}
	if (upperConcrete)
	{
		outLowerEndpointDirection = upperRequired;
		return true;
	}

	return false;
}

FVector FBlueprintVerticalConveyorConnectionManager::
	GetAdjacentLiftVisualDirectionWorld(const FEndpointRef& endpoint) const
{
	AFGBuildableConveyorLift* lift = GetAdjacentLift(endpoint);
	if (!IsValid(lift))
	{
		return FVector::ZeroVector;
	}

	FTransform liftTransform;
	if (endpoint.UsesBlueprintPreviewTransform)
	{
		if (!TryGetBlueprintBuildableWorldTransform(lift, liftTransform))
		{
			return FVector::ZeroVector;
		}
	}
	else
	{
		liftTransform = lift->GetActorTransform();
	}

	FVector direction = liftTransform.GetRotation().GetForwardVector();
	direction.Z = 0.0f;
	return direction.GetSafeNormal();
}

FVector FBlueprintVerticalConveyorConnectionManager::ResolveBridgeVisualDirection(
	const FEndpointRef& blueprintEndpoint,
	const FEndpointRef& targetEndpoint) const
{
	const FVector blueprintDirection =
		GetAdjacentLiftVisualDirectionWorld(blueprintEndpoint);
	const FVector targetDirection =
		GetAdjacentLiftVisualDirectionWorld(targetEndpoint);

	FVector direction = !blueprintDirection.IsNearlyZero()
		? blueprintDirection
		: targetDirection;

	if (direction.IsNearlyZero() &&
		blueprintEndpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		const AFGBuildablePassthrough* hole =
			Cast<AFGBuildablePassthrough>(blueprintEndpoint.Buildable);
		FVector validDirection = FVector::ZeroVector;
		if (IsValid(hole) &&
			hole->GetValidAttachDirection(validDirection) &&
			!validDirection.IsNearlyZero())
		{
			const FVector localDirection =
				hole->GetActorTransform().InverseTransformVectorNoScale(
					validDirection);
			direction = GetBuildableWorldTransform(blueprintEndpoint)
				.TransformVectorNoScale(localDirection);
		}
	}

	if (direction.IsNearlyZero())
	{
		direction = GetBuildableWorldTransform(blueprintEndpoint)
			.GetRotation()
			.GetForwardVector();
	}
	direction.Z = 0.0f;
	if (direction.IsNearlyZero())
	{
		direction = FVector::ForwardVector;
	}

	return direction.GetSafeNormal();
}

TSubclassOf<UFGRecipe>
FBlueprintVerticalConveyorConnectionManager::ResolveLiftRecipe(
	const FEndpointRef& blueprintEndpoint,
	const FEndpointRef& targetEndpoint) const
{
	if (TSubclassOf<UFGRecipe> recipe =
		GetEndpointLiftRecipe(blueprintEndpoint))
	{
		return recipe;
	}
	return GetEndpointLiftRecipe(targetEndpoint);
}

void FBlueprintVerticalConveyorConnectionManager::SpawnBridgeHologram(
	FConnectionState& state,
	int32 stateIndex)
{
	AFGBlueprintHologram* parent = GetHologram();
	if (!IsValid(parent) || !state.LiftRecipe)
	{
		return;
	}

	// AFGHologram keeps a stable name lookup for child holograms. Destroying a
	// child does not synchronously release that name, which is especially easy to
	// hit while rotating a blueprint and switching between candidate lift tiers.
	// Give every state/recipe pair a deterministic child name and cache/reuse that
	// hologram for the lifetime of the parent blueprint hologram.
	if (IsValid(state.BridgeHologram))
	{
		state.BridgeHologram->SetDisabled(true);
	}
	state.BridgeHologram = nullptr;

	UClass* liftRecipe = state.LiftRecipe.Get();
	const FString recipeName = GetNameSafe(liftRecipe);
	const FString recipePath = IsValid(liftRecipe)
		? liftRecipe->GetPathName()
		: FString();

	// Use the complete recipe object path as cache identity. The readable short
	// name is retained only for diagnostics; the deterministic MD5 prevents two
	// recipes from different packages with the same object name from aliasing.
	const FString recipeIdentity = FMD5::HashAnsiString(*recipePath);
	const FName childName(*FString::Printf(
		TEXT("VerticalConveyorAutoBridge_%d_%s_%s"),
		stateIndex,
		*recipeName,
		*recipeIdentity));

	AFGHologram* child = parent->FindChildHologramByName(childName);
	if (child == nullptr)
	{
		// True cache miss: no child is registered under this deterministic name.
		AActor* hologramOwner = IsValid(parent->GetOwner())
			? parent->GetOwner()
			: parent;

		child = AFGHologram::SpawnChildHologramFromRecipe(
			parent,
			childName,
			state.LiftRecipe,
			hologramOwner,
			parent->GetActorLocation());
	}
	else if (!IsValid(child))
	{
		// AFGHologram can retain a child name while the actor is already pending
		// destruction. Treat that as a poisoned cache entry and fail closed for
		// this placement session. Respawning with the same registered name would
		// hit AFGHologram::AddChild's duplicate-name assertion again.
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: stale registered bridge child %s for recipe=%s path=%s state=%d; refusing same-name respawn"),
			*childName.ToString(),
			*recipeName,
			*recipePath,
			stateIndex);
		return;
	}

	state.BridgeHologram = Cast<AFGConveyorLiftHologram>(child);
	if (!IsValid(state.BridgeHologram))
	{
		// Leave an unexpectedly typed child disabled and registered. Destroying it
		// here would recreate the same stale-name problem on a later retry.
		if (IsValid(child))
		{
			child->SetDisabled(true);
		}
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: recipe %s child %s is not a conveyor-lift hologram"),
			*GetNameSafe(state.LiftRecipe.Get()),
			*childName.ToString());
		return;
	}

	state.BridgeHologram->SetShouldSpawnChildHolograms(false);
	state.BridgeHologram->SetBuildModeOverride(
		parent->GetBridgeHologramBuildModeOverride());
	state.BridgeHologram->SetDisabled(true);

}

bool FBlueprintVerticalConveyorConnectionManager::EnsureBridgeRecipe(
	FConnectionState& state,
	int32 stateIndex,
	TSubclassOf<UFGRecipe> recipe)
{
	if (!recipe)
	{
		return false;
	}

	if (state.LiftRecipe != recipe || !IsValid(state.BridgeHologram))
	{
		state.LiftRecipe = recipe;
		SpawnBridgeHologram(state, stateIndex);
	}
	return IsValid(state.BridgeHologram);
}

bool FBlueprintVerticalConveyorConnectionManager::ConfigureBridgeHologram(
	FConnectionState& state,
	bool useConstructedBlueprintBuildable,
	bool finalValidation)
{
	if (!IsValid(state.BridgeHologram) || !IsValid(state.TargetBuildable))
	{
		return false;
	}

	const FEndpointRef blueprintEndpoint =
		GetBlueprintEndpoint(state, useConstructedBlueprintBuildable);
	const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
	if (!IsValid(blueprintEndpoint.Buildable) ||
		!IsValid(targetEndpoint.Buildable))
	{
		return false;
	}

	FEndpointRef lowerEndpoint;
	FEndpointRef upperEndpoint;
	EFactoryConnectionDirection lowerEndpointDirection =
		state.ResolvedLowerEndpointDirection;

	// Final construction runs before the freshly spawned blueprint actors have
	// completed BeginPlay. Their transport-side connection can therefore be
	// transiently FCD_ANY. The connection was already validated and locked during
	// preview, so remap geometry here but retain that transport intent.
	const bool hasLockedTransportDirection =
		(finalValidation || state.HasSnappedTarget) &&
		(lowerEndpointDirection == EFactoryConnectionDirection::FCD_INPUT ||
		 lowerEndpointDirection == EFactoryConnectionDirection::FCD_OUTPUT);
	if (hasLockedTransportDirection)
	{
		const float blueprintZ =
			GetEndpointWorldTransform(blueprintEndpoint).GetLocation().Z;
		const float targetZ =
			GetEndpointWorldTransform(targetEndpoint).GetLocation().Z;
		if (blueprintZ <= targetZ)
		{
			lowerEndpoint = blueprintEndpoint;
			upperEndpoint = targetEndpoint;
		}
		else
		{
			lowerEndpoint = targetEndpoint;
			upperEndpoint = blueprintEndpoint;
		}
	}
	else
	{
		if (!ResolveLowerEndpointDirection(
			blueprintEndpoint,
			targetEndpoint,
			lowerEndpointDirection,
			lowerEndpoint,
			upperEndpoint))
		{
			return false;
		}
		state.ResolvedLowerEndpointDirection = lowerEndpointDirection;
	}

	const bool flowsUpwards =
		lowerEndpointDirection == EFactoryConnectionDirection::FCD_INPUT;
	const FEndpointRef& inputEndpoint =
		flowsUpwards ? lowerEndpoint : upperEndpoint;
	const FEndpointRef& outputEndpoint =
		flowsUpwards ? upperEndpoint : lowerEndpoint;

	FEndpointRef placementStart;
	FEndpointRef placementEnd;
	if (!ResolveLiftPlacementEndpoints(
		inputEndpoint,
		outputEndpoint,
		placementStart,
		placementEnd))
	{
		return false;
	}

	const FVector inputLocation =
		GetEndpointWorldTransform(inputEndpoint).GetLocation();
	const FVector outputLocation =
		GetEndpointWorldTransform(outputEndpoint).GetLocation();
	const FVector placementStartLocation =
		GetEndpointWorldTransform(placementStart).GetLocation();
	const FVector placementEndLocation =
		GetEndpointWorldTransform(placementEnd).GetLocation();
	const FVector visualDirection = ResolveBridgeVisualDirection(
		blueprintEndpoint,
		targetEndpoint);

	FRotator baseRotation = visualDirection.Rotation();
	baseRotation.Pitch = 0.0f;
	baseRotation.Roll = 0.0f;
	const FTransform placementStartTransform(
		baseRotation,
		placementStartLocation,
		FVector::OneVector);
	const FTransform placementEndTransform(
		baseRotation,
		placementEndLocation,
		FVector::OneVector);

	AFGConveyorLiftHologram* bridge = state.BridgeHologram;
	bridge->SetActorTransform(placementStartTransform);
	bridge->mTopTransform =
		placementEndTransform.GetRelativeTransform(placementStartTransform);

	const bool placementStartsAtAttachment =
		placementStart.Kind == EBlueprintVerticalEndpointKind::AttachmentPort;
	const bool placementEndsAtAttachment =
		placementEnd.Kind == EBlueprintVerticalEndpointKind::AttachmentPort;
	EFactoryConnectionDirection forcedAttachmentDirection =
		EFactoryConnectionDirection::FCD_ANY;
	bridge->mSnappedPassthroughs.SetNum(2);

	if (placementStartsAtAttachment)
	{
		// Measured vanilla state: an attachment is placement slot 0. Slot 1 is
		// either a Floor Hole or, when the live hologram exposes that capability,
		// another attachment. Flow is expressed by arrow/reversal, not by swapping
		// placement slots.
		UFGFactoryConnectionComponent* startAttachmentConnection =
			GetDirectEndpointConnection(placementStart);
		if (!IsValid(startAttachmentConnection))
		{
			return false;
		}

		const bool startIsTransportInput =
			MakeEndpointKey(placementStart) == MakeEndpointKey(inputEndpoint);
		if (!PrepareAttachmentEndpointDirection(
				placementStart,
				blueprintEndpoint,
				startIsTransportInput,
				finalValidation,
				forcedAttachmentDirection))
		{
			return false;
		}

		AFGBuildablePassthrough* floorHole = nullptr;
		UFGFactoryConnectionComponent* endAttachmentConnection = nullptr;
		if (placementEndsAtAttachment)
		{
			EFactoryConnectionDirection endAttachmentDirection =
				EFactoryConnectionDirection::FCD_ANY;
			const bool endIsTransportInput =
				MakeEndpointKey(placementEnd) == MakeEndpointKey(inputEndpoint);
			if (!PrepareAttachmentEndpointDirection(
					placementEnd,
					blueprintEndpoint,
					endIsTransportInput,
					finalValidation,
					endAttachmentDirection))
			{
				return false;
			}
			endAttachmentConnection =
				GetDirectEndpointConnection(placementEnd);
			if (!IsValid(endAttachmentConnection) ||
				OppositeDirection(endAttachmentDirection) !=
					forcedAttachmentDirection)
			{
				return false;
			}
		}
		else
		{
			floorHole =
				Cast<AFGBuildablePassthrough>(placementEnd.Buildable);
			if (!IsConveyorFloorHole(floorHole))
			{
				return false;
			}
		}

		bridge->mSnappedPassthroughs[0] = nullptr;
		bridge->mSnappedPassthroughs[1] = floorHole;
		bridge->mSnappedConnectionComponents[0] = startAttachmentConnection;
		// For a Floor Hole, manual vanilla placement stores the continuation
		// across the passthrough in slot 1 when one exists. A bare Floor Hole leaves
		// it null. An attachment endpoint stores its direct vertical connection.
		bridge->mSnappedConnectionComponents[1] = placementEndsAtAttachment
			? endAttachmentConnection
			: GetTransportConnectionAcrossEndpoint(placementEnd);
		bridge->mForcedNormalDirection =
			GetEndpointOutwardNormalWorld(placementStart);
		bridge->mArrowDirection = forcedAttachmentDirection;
		bridge->mIsReversed =
			forcedAttachmentDirection == EFactoryConnectionDirection::FCD_INPUT;
		bridge->mFirstStepYaw = 0.0f;
	}
	else
	{
		// Proven floor-hole-only state remains transport-input first.
		bridge->mSnappedPassthroughs[0] =
			Cast<AFGBuildablePassthrough>(inputEndpoint.Buildable);
		bridge->mSnappedPassthroughs[1] =
			Cast<AFGBuildablePassthrough>(outputEndpoint.Buildable);
		bridge->mSnappedConnectionComponents[0] = nullptr;
		bridge->mSnappedConnectionComponents[1] = nullptr;
		bridge->mForcedNormalDirection = visualDirection;
		bridge->mArrowDirection = EFactoryConnectionDirection::FCD_INPUT;
		bridge->mIsReversed = false;
		bridge->mFirstStepYaw = baseRotation.Yaw;
	}
	bridge->mActivePointIdx = 1;

	bridge->Tags.AddUnique(
		VerticalConveyorAutoConnect::
			PreviewInvalidFloorSuppressionTag());

	bridge->SetDisabled(false);
	bridge->ResetConstructDisqualifiers();
	bridge->UpdateConnectionDirections();
	bridge->OnRep_TopTransform();
	bridge->OnRep_SnappedPassthroughs();

	if (placementStartsAtAttachment)
	{
		// UpdateConnectionDirections() and the transform/passthrough rep callbacks are
		// designed around a normally snapped interactive placement. With a synthetic
		// bridge and a bare attachment port (FCD_ANY), they can erase the transport
		// state we established and leave mArrowDirection as FCD_ANY. Re-apply the
		// FULL state measured from manual vanilla placement AFTER those state-mutating
		// calls, including the hologram's own two connection-component directions.
		// This reconstructs hologram state without mutating the real attachment port.
		UFGFactoryConnectionComponent* ownConnection0 =
			bridge->mConnectionComponents[0].Get();
		UFGFactoryConnectionComponent* ownConnection1 =
			bridge->mConnectionComponents[1].Get();
		const EFactoryConnectionDirection ownDirection0 =
			OppositeDirection(forcedAttachmentDirection);
		if (!IsValid(ownConnection0) ||
			!IsValid(ownConnection1) ||
			ownDirection0 == EFactoryConnectionDirection::FCD_ANY)
		{
			return false;
		}

		ownConnection0->SetDirection(ownDirection0);
		ownConnection1->SetDirection(forcedAttachmentDirection);
		bridge->mArrowDirection = forcedAttachmentDirection;
		bridge->mIsReversed =
			forcedAttachmentDirection == EFactoryConnectionDirection::FCD_INPUT;
	}

	if (!CanConnectLiftToPlacementEnd(bridge, placementEnd))
	{
		return false;
	}

	bridge->OnRep_ArrowDirection();
	bridge->UpdateClearance();
	if (!finalValidation)
	{
		bridge->CheckBlueprintCommingling();
	}
	bridge->CheckValidPlacement();

	TArray<TSubclassOf<UFGConstructDisqualifier>> disqualifiers;
	bridge->GetConstructDisqualifiers(disqualifiers);
	FString disqualifierNames;
	for (const TSubclassOf<UFGConstructDisqualifier>& disqualifier : disqualifiers)
	{
		if (!disqualifierNames.IsEmpty())
		{
			disqualifierNames += TEXT(",");
		}
		disqualifierNames += GetNameSafe(disqualifier.Get());
	}
	if (disqualifierNames.IsEmpty())
	{
		disqualifierNames = TEXT("<none>");
	}

	const bool canConstruct = bridge->CanConstruct();
	const bool placementStartsAtTransportInput =
		MakeEndpointKey(placementStart) == MakeEndpointKey(inputEndpoint);

	if (finalValidation)
	{
		bridge->Tags.Remove(
			VerticalConveyorAutoConnect::
				PreviewInvalidFloorSuppressionTag());
	}

	if (finalValidation)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: final-config %s flow=%s transportInput=%s loc=%s transportOutput=%s loc=%s placementStart=%s placementEnd=%s placementStartIsTransportInput=%d startsAtAttachment=%d endsAtAttachment=%d reversed=%d arrow=%d own0=%d own1=%d snap0=%s snap1=%s pass0=%s pass1=%s spanZ=%.1f canConstruct=%d disqualifiers=[%s]"),
			*GetNameSafe(bridge),
			flowsUpwards ? TEXT("up") : TEXT("down"),
			*DescribeEndpoint(inputEndpoint),
			*inputLocation.ToString(),
			*DescribeEndpoint(outputEndpoint),
			*outputLocation.ToString(),
			*DescribeEndpoint(placementStart),
			*DescribeEndpoint(placementEnd),
			placementStartsAtTransportInput ? 1 : 0,
			placementStartsAtAttachment ? 1 : 0,
			placementEndsAtAttachment ? 1 : 0,
			bridge->mIsReversed ? 1 : 0,
			static_cast<int32>(bridge->mArrowDirection),
			IsValid(bridge->mConnectionComponents[0].Get())
			? static_cast<int32>(bridge->mConnectionComponents[0]->GetDirection())
			: -1,
			IsValid(bridge->mConnectionComponents[1].Get())
			? static_cast<int32>(bridge->mConnectionComponents[1]->GetDirection())
			: -1,
			*GetNameSafe(bridge->mSnappedConnectionComponents[0].Get()),
			*GetNameSafe(bridge->mSnappedConnectionComponents[1].Get()),
			bridge->mSnappedPassthroughs.Num() > 0
			? *GetNameSafe(bridge->mSnappedPassthroughs[0].Get())
			: TEXT("<none>"),
			bridge->mSnappedPassthroughs.Num() > 1
			? *GetNameSafe(bridge->mSnappedPassthroughs[1].Get())
			: TEXT("<none>"),
			bridge->mTopTransform.GetTranslation().Z,
			canConstruct ? 1 : 0,
			*disqualifierNames);
	}

	// Do not reinterpret vanilla disqualifiers. Soft-clearance warnings are
	// intentionally constructible in vanilla; CanConstruct() is the authority.
	return canConstruct;
}

bool FBlueprintVerticalConveyorConnectionManager::ConfigurePreviewBridgeHologram(
	FConnectionState& state)
{
	// Reconfiguring a bridge rebuilds its lift mesh and clearance and reruns
	// vanilla placement validation. While the blueprint and target do not move,
	// every input is unchanged, so reuse the last successful result. Final
	// construction always reconfigures (ConfigureBridgeHologram finalValidation).
	const FEndpointRef blueprintEndpoint = GetBlueprintEndpoint(state, false);
	const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
	const FTransform blueprintTransform =
		GetEndpointWorldTransform(blueprintEndpoint);
	const FTransform targetTransform =
		GetEndpointWorldTransform(targetEndpoint);

	if (state.HasPreviewBridgeCache &&
		IsValid(state.BridgeHologram) &&
		!state.BridgeHologram->IsDisabled() &&
		state.PreviewBridgeHologram == state.BridgeHologram &&
		state.PreviewBridgeRecipe == state.LiftRecipe &&
		state.PreviewBridgeTarget == state.TargetBuildable &&
		state.PreviewBridgeTargetKind == state.TargetKind &&
		state.PreviewBridgeBlueprintSide == state.BlueprintSide &&
		state.PreviewBridgeTargetSide == state.TargetSide &&
		state.PreviewBridgeTargetConnectionName == state.TargetConnectionName &&
		state.PreviewBridgeInputLowerDirection ==
			state.ResolvedLowerEndpointDirection &&
		state.PreviewBridgeSnapped == state.HasSnappedTarget &&
		state.PreviewBridgeBlueprintTransform.Equals(blueprintTransform, 0.0) &&
		state.PreviewBridgeTargetTransform.Equals(targetTransform, 0.0))
	{
		state.ResolvedLowerEndpointDirection =
			state.PreviewBridgeOutputLowerDirection;
		return true;
	}

	const EFactoryConnectionDirection inputLowerDirection =
		state.ResolvedLowerEndpointDirection;
	const bool canConstruct = ConfigureBridgeHologram(state, false, false);
	state.HasPreviewBridgeCache = canConstruct;
	if (canConstruct)
	{
		state.PreviewBridgeHologram = state.BridgeHologram;
		state.PreviewBridgeRecipe = state.LiftRecipe;
		state.PreviewBridgeTarget = state.TargetBuildable;
		state.PreviewBridgeTargetKind = state.TargetKind;
		state.PreviewBridgeBlueprintSide = state.BlueprintSide;
		state.PreviewBridgeTargetSide = state.TargetSide;
		state.PreviewBridgeTargetConnectionName = state.TargetConnectionName;
		state.PreviewBridgeInputLowerDirection = inputLowerDirection;
		state.PreviewBridgeOutputLowerDirection =
			state.ResolvedLowerEndpointDirection;
		state.PreviewBridgeSnapped = state.HasSnappedTarget;
		state.PreviewBridgeBlueprintTransform = blueprintTransform;
		state.PreviewBridgeTargetTransform = targetTransform;
	}
	return canConstruct;
}

void FBlueprintVerticalConveyorConnectionManager::DisableBridge(
	FConnectionState& state)
{
	if (IsValid(state.BridgeHologram))
	{
		state.BridgeHologram->SetDisabled(true);
	}
}

void FBlueprintVerticalConveyorConnectionManager::ClearTarget(
	FConnectionState& state)
{
	state.TargetBuildable = nullptr;
	state.TargetConnectionName = NAME_None;
	state.TargetKind = EBlueprintVerticalEndpointKind::FloorHoleSide;
	state.TargetSide = EBlueprintVerticalEndpointSide::Bottom;
	state.ResolvedLowerEndpointDirection =
		EFactoryConnectionDirection::FCD_ANY;
	state.DirectBlueprintConnectionDirection =
		EFactoryConnectionDirection::FCD_ANY;
	state.DirectTargetConnectionDirection =
		EFactoryConnectionDirection::FCD_ANY;
	state.CanDirectlyConnect = false;
	state.IsValid = false;
}

bool FBlueprintVerticalConveyorConnectionManager::IsAttachmentPortForSpan(
	const FEndpointRef& attachmentEndpoint,
	const FEndpointRef& otherEndpoint) const
{
	if (attachmentEndpoint.Kind !=
		EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		return true;
	}

	const float attachmentZ =
		GetEndpointWorldTransform(attachmentEndpoint).GetLocation().Z;
	const float otherZ =
		GetEndpointWorldTransform(otherEndpoint).GetLocation().Z;
	if (FMath::IsNearlyEqual(attachmentZ, otherZ))
	{
		return true;
	}

	// Measured vanilla behavior:
	//   attachment below floor hole  -> snap0 = TopConnection
	//   attachment above floor hole  -> snap0 = BottomConnection
	//
	// AFGConveyorAttachmentHologram's named lift-connection constants resolve to
	// those component identities in the opposite-looking order used by the
	// buildable. Use the vanilla/API identities rather than literal component
	// strings so this remains tied to the game's own attachment definition.
	const FName requiredConnectionName =
		attachmentZ < otherZ
			? AFGConveyorAttachmentHologram::mLiftConnection_Bottom
			: AFGConveyorAttachmentHologram::mLiftConnection_Top;

	return attachmentEndpoint.ConnectionName == requiredConnectionName;
}

void FBlueprintVerticalConveyorConnectionManager::NormalizeAttachmentFacingSides(
	FEndpointRef& first,
	FEndpointRef& second) const
{
	const float firstZ = GetEndpointWorldTransform(first).GetLocation().Z;
	const float secondZ = GetEndpointWorldTransform(second).GetLocation().Z;
	if (FMath::IsNearlyEqual(firstZ, secondZ))
	{
		return;
	}

	// Floor-hole top/bottom is an intrinsic side of the passthrough. A vertical
	// attachment connection is different: the component identity is intrinsic,
	// while the side it faces for THIS lift span follows the other endpoint's Z.
	// This is why the same open splitter/merger port can legitimately accept a
	// lift arriving from above or below in vanilla.
	if (first.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		first.Side = firstZ < secondZ
			? EBlueprintVerticalEndpointSide::Top
			: EBlueprintVerticalEndpointSide::Bottom;
	}
	if (second.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		second.Side = secondZ < firstZ
			? EBlueprintVerticalEndpointSide::Top
			: EBlueprintVerticalEndpointSide::Bottom;
	}
}

bool FBlueprintVerticalConveyorConnectionManager::IsGeometricallyCompatible(
	const FEndpointRef& blueprintEndpoint,
	const FEndpointRef& targetEndpoint,
	float& outVerticalDistance,
	bool& outCanDirectlyConnect) const
{
	const FVector blueprintLocation =
		GetEndpointWorldTransform(blueprintEndpoint).GetLocation();
	const FVector targetLocation =
		GetEndpointWorldTransform(targetEndpoint).GetLocation();
	const FVector delta = targetLocation - blueprintLocation;

	// These endpoints are required to occupy the same physical XY column.
	// Use Unreal's own numerical-equality semantics only; do not widen this into
	// a mod-defined snapping radius.
	if (!FMath::IsNearlyZero(delta.X) ||
		!FMath::IsNearlyZero(delta.Y))
	{
		return false;
	}

	outVerticalDistance = FMath::Abs(delta.Z);
	outCanDirectlyConnect = delta.IsNearlyZero();
	if (outCanDirectlyConnect)
	{
		const FVector firstNormal =
			GetEndpointOutwardNormalWorld(blueprintEndpoint).GetSafeNormal();
		const FVector secondNormal =
			GetEndpointOutwardNormalWorld(targetEndpoint).GetSafeNormal();

		// Direct connection requires genuinely opposing endpoint normals. Again,
		// use FVector's engine-defined equality tolerance rather than inventing
		// an angular allowance.
		return firstNormal.Equals(-secondNormal);
	}

	// Do not pre-limit vertical span here. The generated vanilla conveyor-lift
	// hologram owns the validity decision for whatever span Satisfactory allows.
	if (delta.Z > 0.0f)
	{
		return blueprintEndpoint.Side == EBlueprintVerticalEndpointSide::Top &&
			targetEndpoint.Side == EBlueprintVerticalEndpointSide::Bottom;
	}
	return blueprintEndpoint.Side == EBlueprintVerticalEndpointSide::Bottom &&
		targetEndpoint.Side == EBlueprintVerticalEndpointSide::Top;
}

void FBlueprintVerticalConveyorConnectionManager::FindBestTarget(
	FConnectionState& state,
	int32 stateIndex,
	const TSet<FEndpointKey>& claimedTargetEndpoints,
	const TArray<FTargetEndpointCandidate>& targetEndpoints)
{
	if (state.HasSnappedTarget && IsValid(state.TargetBuildable))
	{
		return;
	}
	if (state.HasSnappedTarget)
	{
		// Match vanilla's state lifecycle: a destroyed/unloaded locked target
		// releases the first-click lock instead of silently carrying it to a new
		// candidate.
		state.HasSnappedTarget = false;
	}

	ClearTarget(state);
	const FEndpointRef rawBlueprintEndpoint = GetBlueprintEndpoint(state, false);

	if (!IsEndpointOpen(rawBlueprintEndpoint))
	{
		DisableBridge(state);
		return;
	}

	struct FPhysicalCandidate
	{
		FEndpointRef BlueprintEndpoint;
		FEndpointRef Endpoint;
		float VerticalDistance = 0.0f;
		bool CanDirectlyConnect = false;
	};

	// Discover physical endpoints first, including occupied ones. A vertical
	// splitter/merger or floor-hole endpoint physically terminates the search
	// column: if the nearest endpoint cannot be used, do not tunnel through it
	// to a farther endpoint at the same X/Y coordinates.
	TArray<FPhysicalCandidate> physicalCandidates;
	const FVector blueprintLocation =
		GetEndpointWorldTransform(rawBlueprintEndpoint).GetLocation();
	for (const FTargetEndpointCandidate& targetCandidate : targetEndpoints)
	{
		const FEndpointRef& rawTargetEndpoint = targetCandidate.Endpoint;
		if (!IsValid(rawTargetEndpoint.Buildable))
		{
			continue;
		}

		// Same XY-column test IsGeometricallyCompatible applies below. Endpoint
		// locations do not depend on Side, so the facing normalization cannot
		// change its outcome; testing it first skips other columns cheaply.
		if (!FMath::IsNearlyZero(targetCandidate.Location.X - blueprintLocation.X) ||
			!FMath::IsNearlyZero(targetCandidate.Location.Y - blueprintLocation.Y))
		{
			continue;
		}

		// Attachment port identity is not interchangeable. Manual vanilla
		// placement selects TopConnection for an upward span from the
		// attachment and BottomConnection for a downward span. Filtering the
		// wrong identity here also prevents it from claiming the floor-hole
		// target before the correct sibling endpoint is evaluated.
		if (!IsAttachmentPortForSpan(
				rawBlueprintEndpoint,
				rawTargetEndpoint) ||
			!IsAttachmentPortForSpan(
				rawTargetEndpoint,
				rawBlueprintEndpoint))
		{
			continue;
		}

		FEndpointRef effectiveBlueprintEndpoint = rawBlueprintEndpoint;
		FEndpointRef effectiveTargetEndpoint = rawTargetEndpoint;
		NormalizeAttachmentFacingSides(
			effectiveBlueprintEndpoint,
			effectiveTargetEndpoint);

		float verticalDistance = 0.0f;
		bool canDirectlyConnect = false;
		if (!IsGeometricallyCompatible(
			effectiveBlueprintEndpoint,
			effectiveTargetEndpoint,
			verticalDistance,
			canDirectlyConnect))
		{
			continue;
		}
		FEndpointRef blockingBlueprintEndpoint;
		if (!canDirectlyConnect &&
			HasInterveningBlueprintEndpoint(
				effectiveBlueprintEndpoint,
				effectiveTargetEndpoint,
				&blockingBlueprintEndpoint))
		{
			if (!LoggedSourceTunnelStates.Contains(stateIndex))
			{
				LoggedSourceTunnelStates.Add(stateIndex);
				UE_LOG(
					LogVerticalConveyorAutoConnect,
					Verbose,
					TEXT("VerticalConveyorAutoConnect: source-side no-tunnel state=%d source=%s blocker=%s target=%s"),
					stateIndex,
					*DescribeEndpoint(effectiveBlueprintEndpoint),
					*DescribeEndpoint(blockingBlueprintEndpoint),
					*DescribeEndpoint(effectiveTargetEndpoint));
			}
			continue;
		}

		FPhysicalCandidate& candidate =
			physicalCandidates.AddDefaulted_GetRef();
		candidate.BlueprintEndpoint = effectiveBlueprintEndpoint;
		candidate.Endpoint = effectiveTargetEndpoint;
		candidate.VerticalDistance = verticalDistance;
		candidate.CanDirectlyConnect = canDirectlyConnect;
	}

	physicalCandidates.Sort(
		[](const FPhysicalCandidate& first, const FPhysicalCandidate& second)
		{
			if (!FMath::IsNearlyEqual(
				first.VerticalDistance,
				second.VerticalDistance))
			{
				return first.VerticalDistance < second.VerticalDistance;
			}
			const FString firstName = IsValid(first.Endpoint.Buildable)
				? first.Endpoint.Buildable->GetPathName()
				: FString();
			const FString secondName = IsValid(second.Endpoint.Buildable)
				? second.Endpoint.Buildable->GetPathName()
				: FString();
			if (firstName != secondName)
			{
				return firstName.Compare(secondName, ESearchCase::CaseSensitive) < 0;
			}
			if (first.Endpoint.Kind != second.Endpoint.Kind)
			{
				return static_cast<uint8>(first.Endpoint.Kind) <
					static_cast<uint8>(second.Endpoint.Kind);
			}
			if (first.Endpoint.Side != second.Endpoint.Side)
			{
				return static_cast<uint8>(first.Endpoint.Side) <
					static_cast<uint8>(second.Endpoint.Side);
			}
			return first.Endpoint.ConnectionName.ToString().Compare(
				second.Endpoint.ConnectionName.ToString(),
				ESearchCase::CaseSensitive) < 0;
		});

	if (physicalCandidates.IsEmpty())
	{
		DisableBridge(state);
		return;
	}

	const float nearestDistance = physicalCandidates[0].VerticalDistance;
	for (const FPhysicalCandidate& physical : physicalCandidates)
	{
		if (!FMath::IsNearlyEqual(
			physical.VerticalDistance,
			nearestDistance))
		{
			break;
		}

		const FEndpointRef& blueprintEndpoint = physical.BlueprintEndpoint;
		const FEndpointRef& targetEndpoint = physical.Endpoint;
		if (!IsEndpointOpen(targetEndpoint))
		{
			continue;
		}
		if (claimedTargetEndpoints.Contains(MakeEndpointKey(targetEndpoint)))
		{
			continue;
		}

		const TSubclassOf<UFGRecipe> recipe =
			ResolveLiftRecipe(blueprintEndpoint, targetEndpoint);
		if (!recipe)
		{
			continue;
		}

		EFactoryConnectionDirection resolvedLowerDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection directBlueprintDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection directTargetDirection =
			EFactoryConnectionDirection::FCD_ANY;
		if (physical.CanDirectlyConnect)
		{
			UFGFactoryConnectionComponent* first =
				GetTransportConnectionAcrossEndpoint(blueprintEndpoint);
			UFGFactoryConnectionComponent* second =
				GetTransportConnectionAcrossEndpoint(targetEndpoint);
			const bool isReciprocal = IsValid(first) && IsValid(second) &&
				first->GetConnection() == second &&
				second->GetConnection() == first &&
				first->IsConnected() && second->IsConnected();
			const bool isFresh = IsValid(first) && IsValid(second) &&
				first->GetConnection() == nullptr &&
				second->GetConnection() == nullptr &&
				!first->IsConnected() && !second->IsConnected();
			if ((!isReciprocal && !isFresh) ||
				!ResolveDirectConnectionDirections(
					first,
					second,
					directBlueprintDirection,
					directTargetDirection) ||
				(isFresh && !first->CanConnectTo(second) &&
				 !second->CanConnectTo(first)))
			{
				continue;
			}
		}
		else
		{
			FEndpointRef lower;
			FEndpointRef upper;
			if (!ResolveLowerEndpointDirection(
				blueprintEndpoint,
				targetEndpoint,
				resolvedLowerDirection,
				lower,
				upper))
			{
				continue;
			}
		}

		// Persist the geometry-derived attachment facing side as part of the locked
		// candidate state. Floor-hole sides remain unchanged/intrinsic.
		state.BlueprintSide = blueprintEndpoint.Side;
		state.TargetBuildable = targetEndpoint.Buildable;
		state.TargetKind = targetEndpoint.Kind;
		state.TargetSide = targetEndpoint.Side;
		state.TargetConnectionName = targetEndpoint.ConnectionName;
		state.ResolvedLowerEndpointDirection = resolvedLowerDirection;
		state.DirectBlueprintConnectionDirection = directBlueprintDirection;
		state.DirectTargetConnectionDirection = directTargetDirection;
		state.CanDirectlyConnect = physical.CanDirectlyConnect;

		if (physical.CanDirectlyConnect)
		{
			state.LiftRecipe = recipe;
			state.IsValid = true;
			DisableBridge(state);
			return;
		}

		if (EnsureBridgeRecipe(state, stateIndex, recipe) &&
			ConfigurePreviewBridgeHologram(state))
		{
			state.IsValid = true;
			return;
		}
	}

	// The nearest physical endpoint(s) existed but none were usable. Intentionally
	// stop here instead of considering farther targets in the same vertical column.
	ClearTarget(state);
	DisableBridge(state);
}

void FBlueprintVerticalConveyorConnectionManager::
	BroadcastConnectionStateChange(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& previousTargetEndpoint,
		const FEndpointRef& targetEndpoint,
		bool isValid)
{
	TArray<UFGConnectionComponent*> blueprintConnections;
	if (UFGFactoryConnectionComponent* connection =
		GetBlueprintRepresentationConnection(blueprintEndpoint))
	{
		blueprintConnections.Add(connection);
	}
	if (blueprintConnections.IsEmpty())
	{
		// A bare Floor Hole has no factory connection component (and therefore no
		// ordinary direction indicator) for the parent hologram to replace.
		return;
	}

	UFGFactoryConnectionComponent* previousTargetConnection =
		GetTransportConnectionAcrossEndpoint(previousTargetEndpoint);
	UFGFactoryConnectionComponent* targetConnection =
		GetTransportConnectionAcrossEndpoint(targetEndpoint);

	// This is the same delegate contract used by vanilla's manager. The parent
	// hologram owns the automatic-link representation and swaps the ordinary
	// connection-direction visualization when a state becomes valid.
	mOnConnectionStateChanged.Broadcast(
		blueprintConnections,
		previousTargetConnection,
		targetConnection,
		isValid);
}

void FBlueprintVerticalConveyorConnectionManager::UpdateAutomaticConnections(
	const FHitResult& /*hitResult*/,
	bool& outPlaySnapEffects)
{
	const double startSeconds = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		const double elapsedSeconds = FPlatformTime::Seconds() - startSeconds;
		++UpdateTimingCount;
		UpdateTimingTotalSeconds += elapsedSeconds;
		UpdateTimingMaxSeconds = FMath::Max(UpdateTimingMaxSeconds, elapsedSeconds);
		if (UpdateTimingCount >= UpdateTimingLogInterval)
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Verbose,
				TEXT("VerticalConveyorAutoConnect: update timing states=%d calls=%d avg=%.2f ms max=%.2f ms"),
				ConnectionStates.Num(),
				UpdateTimingCount,
				UpdateTimingTotalSeconds * 1000.0 / UpdateTimingCount,
				UpdateTimingMaxSeconds * 1000.0);
			UpdateTimingCount = 0;
			UpdateTimingTotalSeconds = 0.0;
			UpdateTimingMaxSeconds = 0.0;
		}
	};

	NearbyBuildables.RemoveAllSwap(
		[](const TWeakObjectPtr<AFGBuildable>& buildable)
		{
			return !buildable.IsValid();
		});

	TArray<AFGBuildable*> discoveryBuildables;
	GatherDiscoveryCandidates(discoveryBuildables);

	// Gather world endpoints once for all states instead of once per state.
	TArray<FTargetEndpointCandidate> targetEndpoints;
	TArray<FEndpointRef> buildableEndpoints;
	for (AFGBuildable* buildable : discoveryBuildables)
	{
		buildableEndpoints.Reset();
		GatherEndpoints(buildable, false, false, buildableEndpoints);
		for (const FEndpointRef& endpoint : buildableEndpoints)
		{
			FTargetEndpointCandidate& candidate =
				targetEndpoints.AddDefaulted_GetRef();
			candidate.Endpoint = endpoint;
			candidate.Location =
				GetEndpointWorldTransform(endpoint).GetLocation();
		}
	}

	TSet<FEndpointKey> claimedTargetEndpoints;
	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		AFGBuildable* previousTarget = state.TargetBuildable;
		const FName previousConnectionName = state.TargetConnectionName;
		const EBlueprintVerticalEndpointKind previousTargetKind =
			state.TargetKind;
		const EBlueprintVerticalEndpointSide previousTargetSide =
			state.TargetSide;
		const bool previousValid = state.IsValid;
		const FEndpointRef previousTargetEndpoint = GetTargetEndpoint(state);

		FindBestTarget(
			state,
			stateIndex,
			claimedTargetEndpoints,
			targetEndpoints);
		if (state.IsValid && IsValid(state.TargetBuildable))
		{
			claimedTargetEndpoints.Add(MakeEndpointKey(GetTargetEndpoint(state)));
		}

		if (previousTarget != state.TargetBuildable ||
			previousConnectionName != state.TargetConnectionName ||
			previousTargetKind != state.TargetKind ||
			previousTargetSide != state.TargetSide ||
			previousValid != state.IsValid)
		{
			BroadcastConnectionStateChange(
				GetBlueprintEndpoint(state, false),
				previousTargetEndpoint,
				GetTargetEndpoint(state),
				state.IsValid);
			outPlaySnapEffects = outPlaySnapEffects || state.IsValid;
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Verbose,
				TEXT("VerticalConveyorAutoConnect: state=%d source=%s loc=%s target=%s loc=%s valid=%d direct=%d flow=%s recipe=%s"),
				stateIndex,
				*DescribeEndpoint(GetBlueprintEndpoint(state, false)),
				*GetEndpointWorldTransform(GetBlueprintEndpoint(state, false)).GetLocation().ToString(),
				*DescribeEndpoint(GetTargetEndpoint(state)),
				*GetEndpointWorldTransform(GetTargetEndpoint(state)).GetLocation().ToString(),
				state.IsValid ? 1 : 0,
				state.CanDirectlyConnect ? 1 : 0,
				state.ResolvedLowerEndpointDirection == EFactoryConnectionDirection::FCD_INPUT
					? TEXT("up")
					: state.ResolvedLowerEndpointDirection == EFactoryConnectionDirection::FCD_OUTPUT
						? TEXT("down")
						: TEXT("any"),
				*GetNameSafe(state.LiftRecipe.Get()));
		}
	}
}

bool FBlueprintVerticalConveyorConnectionManager::AttemptConnectionStateSnap()
{
	bool snappedAny = false;
	for (FConnectionState& state : ConnectionStates)
	{
		if (state.IsValid && IsValid(state.TargetBuildable) &&
			!state.HasSnappedTarget)
		{
			state.HasSnappedTarget = true;
			snappedAny = true;
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Verbose,
				TEXT("VerticalConveyorAutoConnect: locked %s -> %s; next click confirms the blueprint"),
				*DescribeEndpoint(GetBlueprintEndpoint(state, false)),
				*DescribeEndpoint(GetTargetEndpoint(state)));


		}
	}
	return snappedAny;
}

bool FBlueprintVerticalConveyorConnectionManager::CanSnapConnectionStates() const
{
	for (const FConnectionState& state : ConnectionStates)
	{
		if (state.IsValid && IsValid(state.TargetBuildable) &&
			!state.HasSnappedTarget)
		{
			return true;
		}
	}
	return false;
}

void FBlueprintVerticalConveyorConnectionManager::ResetAutomaticConnections()
{
	for (FConnectionState& state : ConnectionStates)
	{
		const FEndpointRef blueprintEndpoint =
			GetBlueprintEndpoint(state, false);
		const FEndpointRef previousTargetEndpoint = GetTargetEndpoint(state);
		const bool shouldBroadcast =
			state.IsValid || IsValid(state.TargetBuildable);
		ClearTarget(state);
		state.ConstructedBlueprintBuildable = nullptr;
		state.ConstructedBlueprintContinuationConnection = nullptr;
		state.HasSnappedTarget = false;
		DisableBridge(state);
		if (shouldBroadcast)
		{
			BroadcastConnectionStateChange(
				blueprintEndpoint,
				previousTargetEndpoint,
				GetTargetEndpoint(state),
				false);
		}
	}
}

void FBlueprintVerticalConveyorConnectionManager::
	HandleBuildableConnectionRemapping(
		AFGBuildable* buildable,
		int32 blueprintBuildableIndex)
{
	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		if (state.BlueprintContinuationBuildableIndex ==
				blueprintBuildableIndex &&
			state.BlueprintContinuationConnectionIndex != INDEX_NONE)
		{
			AFGBuildableConveyorLift* continuationLift =
				Cast<AFGBuildableConveyorLift>(buildable);
			UFGFactoryConnectionComponent* continuation = nullptr;
			if (IsValid(continuationLift) &&
				state.BlueprintContinuationConnectionIndex == 0)
			{
				continuation = continuationLift->GetConnection0();
			}
			else if (IsValid(continuationLift) &&
				state.BlueprintContinuationConnectionIndex == 1)
			{
				continuation = continuationLift->GetConnection1();
			}
			if (IsValid(continuation))
			{
				state.ConstructedBlueprintContinuationConnection =
					continuation;
				UE_LOG(
					LogVerticalConveyorAutoConnect,
					Verbose,
					TEXT("VerticalConveyorAutoConnect: remapped source continuation state=%d buildableIndex=%d connectionIndex=%d connection=%s"),
					stateIndex,
					blueprintBuildableIndex,
					state.BlueprintContinuationConnectionIndex,
					*GetNameSafe(continuation));
			}
			else
			{
				state.ConstructedBlueprintContinuationConnection = nullptr;
				UE_LOG(
					LogVerticalConveyorAutoConnect,
					Warning,
					TEXT("VerticalConveyorAutoConnect: failed to remap source continuation state=%d buildableIndex=%d connectionIndex=%d buildable=%s"),
					stateIndex,
					blueprintBuildableIndex,
					state.BlueprintContinuationConnectionIndex,
					*GetNameSafe(buildable));
			}
		}

		if (state.BlueprintBuildableIndex == blueprintBuildableIndex &&
			IsSupportedBuildable(buildable))
		{
			state.ConstructedBlueprintBuildable = buildable;
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Verbose,
				TEXT("VerticalConveyorAutoConnect: remapped state=%d index=%d source=%s constructed=%s"),
				stateIndex,
				blueprintBuildableIndex,
				*GetNameSafe(state.BlueprintBuildable),
				*GetNameSafe(state.ConstructedBlueprintBuildable));
		}
	}
}

void FBlueprintVerticalConveyorConnectionManager::SerializeConstructMessage(
	FArchive& archive,
	FNetConstructionID /*id*/)
{
	for (FConnectionState& state : ConnectionStates)
	{
		archive << state;
	}
}

void FBlueprintVerticalConveyorConnectionManager::
	PostConstructMessageDeserialization()
{
	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		if (!IsValid(state.TargetBuildable))
		{
			state.IsValid = false;
			DisableBridge(state);
			continue;
		}

		const FEndpointRef blueprintEndpoint = GetBlueprintEndpoint(state, false);
		const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
		const TSubclassOf<UFGRecipe> recipe =
			ResolveLiftRecipe(blueprintEndpoint, targetEndpoint);
		if (!recipe)
		{
			state.IsValid = false;
			DisableBridge(state);
			continue;
		}

		if (state.CanDirectlyConnect)
		{
			state.LiftRecipe = recipe;
			state.IsValid = true;
			DisableBridge(state);
		}
		else
		{
			state.IsValid =
				EnsureBridgeRecipe(state, stateIndex, recipe) &&
				ConfigureBridgeHologram(state, false, false);
			if (!state.IsValid)
			{
				DisableBridge(state);
			}
		}
	}
}

bool FBlueprintVerticalConveyorConnectionManager::PrepareDirectConnectionPlan(
	const FConnectionState& state,
	FDirectConnectionPlan& outPlan) const
{
	outPlan = {};
	outPlan.BlueprintEndpoint = GetBlueprintEndpoint(state, true);
	outPlan.TargetEndpoint = GetTargetEndpoint(state);
	outPlan.ExpectedBlueprintDirection =
		state.DirectBlueprintConnectionDirection;
	outPlan.ExpectedTargetDirection = state.DirectTargetConnectionDirection;

	const auto isTransportDirection = [](EFactoryConnectionDirection direction)
	{
		return direction == EFactoryConnectionDirection::FCD_INPUT ||
			direction == EFactoryConnectionDirection::FCD_OUTPUT;
	};
	if (!IsValid(outPlan.BlueprintEndpoint.Buildable) ||
		!IsValid(outPlan.TargetEndpoint.Buildable) ||
		!isTransportDirection(outPlan.ExpectedBlueprintDirection) ||
		!isTransportDirection(outPlan.ExpectedTargetDirection) ||
		OppositeDirection(outPlan.ExpectedBlueprintDirection) !=
			outPlan.ExpectedTargetDirection)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=invalid-endpoint-or-locked-direction source=%s target=%s sourceDirection=%d targetDirection=%d"),
			*DescribeEndpoint(outPlan.BlueprintEndpoint),
			*DescribeEndpoint(outPlan.TargetEndpoint),
			static_cast<int32>(outPlan.ExpectedBlueprintDirection),
			static_cast<int32>(outPlan.ExpectedTargetDirection));
		return false;
	}

	float verticalDistance = 0.0f;
	bool canDirectlyConnect = false;
	if (!IsGeometricallyCompatible(
			outPlan.BlueprintEndpoint,
			outPlan.TargetEndpoint,
			verticalDistance,
			canDirectlyConnect) ||
		!canDirectlyConnect)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=final-geometry-changed source=%s sourceLocation=%s target=%s targetLocation=%s distance=%.6f"),
			*DescribeEndpoint(outPlan.BlueprintEndpoint),
			*GetEndpointWorldTransform(outPlan.BlueprintEndpoint)
				.GetLocation()
				.ToString(),
			*DescribeEndpoint(outPlan.TargetEndpoint),
			*GetEndpointWorldTransform(outPlan.TargetEndpoint)
				.GetLocation()
				.ToString(),
			verticalDistance);
		return false;
	}

	outPlan.BlueprintConnection =
		GetTransportConnectionAcrossEndpoint(outPlan.BlueprintEndpoint);
	outPlan.TargetConnection =
		GetTransportConnectionAcrossEndpoint(outPlan.TargetEndpoint);
	if (!IsValid(outPlan.BlueprintConnection) ||
		!IsValid(outPlan.TargetConnection) ||
		outPlan.BlueprintConnection == outPlan.TargetConnection)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=missing-or-shared-connection source=%s sourceConnection=%s target=%s targetConnection=%s"),
			*DescribeEndpoint(outPlan.BlueprintEndpoint),
			*GetNameSafe(outPlan.BlueprintConnection),
			*DescribeEndpoint(outPlan.TargetEndpoint),
			*GetNameSafe(outPlan.TargetConnection));
		return false;
	}

	auto captureFloorHole = [this](
		const TCHAR* endpointName,
		const FEndpointRef& endpoint,
		UFGFactoryConnectionComponent*& outExposedConnection)
	{
		outExposedConnection = nullptr;
		if (endpoint.Kind != EBlueprintVerticalEndpointKind::FloorHoleSide)
		{
			return true;
		}

		AFGBuildablePassthrough* floorHole =
			Cast<AFGBuildablePassthrough>(endpoint.Buildable);
		UFGFactoryConnectionComponent* continuation =
			GetTransportConnectionAcrossEndpoint(endpoint);
		AFGBuildableConveyorLift* continuationLift = IsValid(continuation)
			? Cast<AFGBuildableConveyorLift>(continuation->GetOwner())
			: nullptr;
		if (!IsConveyorFloorHole(floorHole) ||
			!IsValid(continuationLift) ||
			(continuation != continuationLift->GetConnection0() &&
			 continuation != continuationLift->GetConnection1()))
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: direct preflight rejected endpoint=%s reason=invalid-floor-hole-continuation floorHole=%s continuation=%s owner=%s"),
				endpointName,
				*GetNameSafe(floorHole),
				*GetNameSafe(continuation),
				IsValid(continuation)
					? *GetNameSafe(continuation->GetOwner())
					: TEXT("<none>"));
			return false;
		}

		outExposedConnection =
			GetFloorHoleSnappedConnection(floorHole, endpoint.Side);
		return true;
	};
	if (!captureFloorHole(
			TEXT("source"),
			outPlan.BlueprintEndpoint,
			outPlan.OriginalBlueprintExposedConnection) ||
		!captureFloorHole(
			TEXT("target"),
			outPlan.TargetEndpoint,
			outPlan.OriginalTargetExposedConnection))
	{
		return false;
	}

	if (outPlan.BlueprintEndpoint.Kind ==
			EBlueprintVerticalEndpointKind::FloorHoleSide &&
		(!state.BlueprintContinuationWasPresent ||
		 state.BlueprintContinuationBuildableIndex == INDEX_NONE ||
		 !IsValid(state.ConstructedBlueprintContinuationConnection) ||
		 outPlan.BlueprintConnection !=
			 state.ConstructedBlueprintContinuationConnection))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=source-continuation-remap-mismatch source=%s floorHoleTo=%s remapped=%s sourceIndex=%d connectionIndex=%d"),
			*DescribeEndpoint(outPlan.BlueprintEndpoint),
			*GetNameSafe(outPlan.BlueprintConnection),
			*GetNameSafe(state.ConstructedBlueprintContinuationConnection),
			state.BlueprintContinuationBuildableIndex,
			state.BlueprintContinuationConnectionIndex);
		return false;
	}

	UFGFactoryConnectionComponent* blueprintPeer =
		outPlan.BlueprintConnection->GetConnection();
	UFGFactoryConnectionComponent* targetPeer =
		outPlan.TargetConnection->GetConnection();
	const bool isReciprocal = blueprintPeer == outPlan.TargetConnection &&
		targetPeer == outPlan.BlueprintConnection;
	const bool isFresh = blueprintPeer == nullptr && targetPeer == nullptr &&
		!outPlan.BlueprintConnection->IsConnected() &&
		!outPlan.TargetConnection->IsConnected();
	if (isReciprocal)
	{
		if (!outPlan.BlueprintConnection->IsConnected() ||
			!outPlan.TargetConnection->IsConnected())
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=reciprocal-pointer-flag-mismatch sourceConnected=%d targetConnected=%d"),
				outPlan.BlueprintConnection->IsConnected() ? 1 : 0,
				outPlan.TargetConnection->IsConnected() ? 1 : 0);
			return false;
		}
		outPlan.WasAlreadyLinked = true;
	}
	else if (!isFresh)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=non-reciprocal-or-conflicting-link source=%s sourceTo=%s sourceConnected=%d target=%s targetTo=%s targetConnected=%d"),
			*GetNameSafe(outPlan.BlueprintConnection),
			*GetNameSafe(blueprintPeer),
			outPlan.BlueprintConnection->IsConnected() ? 1 : 0,
			*GetNameSafe(outPlan.TargetConnection),
			*GetNameSafe(targetPeer),
			outPlan.TargetConnection->IsConnected() ? 1 : 0);
		return false;
	}

	const UFGFactoryConnectionComponent* expectedBlueprintExposed =
		outPlan.WasAlreadyLinked ? outPlan.TargetConnection : nullptr;
	const UFGFactoryConnectionComponent* expectedTargetExposed =
		outPlan.WasAlreadyLinked ? outPlan.BlueprintConnection : nullptr;
	if ((outPlan.BlueprintEndpoint.Kind ==
			 EBlueprintVerticalEndpointKind::FloorHoleSide &&
		 outPlan.OriginalBlueprintExposedConnection !=
			 expectedBlueprintExposed) ||
		(outPlan.TargetEndpoint.Kind ==
			 EBlueprintVerticalEndpointKind::FloorHoleSide &&
		 outPlan.OriginalTargetExposedConnection != expectedTargetExposed))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=floor-hole-exposed-side-changed sourceExposed=%s expectedSource=%s targetExposed=%s expectedTarget=%s"),
			*GetNameSafe(outPlan.OriginalBlueprintExposedConnection),
			*GetNameSafe(expectedBlueprintExposed),
			*GetNameSafe(outPlan.OriginalTargetExposedConnection),
			*GetNameSafe(expectedTargetExposed));
		return false;
	}

	outPlan.OriginalBlueprintDirection =
		outPlan.BlueprintConnection->GetDirection();
	outPlan.OriginalTargetDirection = outPlan.TargetConnection->GetDirection();
	if (!outPlan.WasAlreadyLinked &&
		outPlan.BlueprintEndpoint.Kind ==
			EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		if (!RestorePersistedAttachmentDirectionBeforeConstruct(
				outPlan.BlueprintEndpoint,
				outPlan.ExpectedBlueprintDirection))
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=source-attachment-direction-restore-failed source=%s expected=%d"),
				*DescribeEndpoint(outPlan.BlueprintEndpoint),
				static_cast<int32>(outPlan.ExpectedBlueprintDirection));
			return false;
		}
		outPlan.ChangedBlueprintDirection =
			outPlan.BlueprintConnection->GetDirection() !=
				outPlan.OriginalBlueprintDirection;
	}

	const auto directionMatches = [](
		EFactoryConnectionDirection actual,
		EFactoryConnectionDirection expected)
	{
		return actual == EFactoryConnectionDirection::FCD_ANY ||
			actual == expected;
	};
	if (!directionMatches(
			outPlan.BlueprintConnection->GetDirection(),
			outPlan.ExpectedBlueprintDirection) ||
		!directionMatches(
			outPlan.TargetConnection->GetDirection(),
			outPlan.ExpectedTargetDirection) ||
		(outPlan.WasAlreadyLinked &&
		 (outPlan.BlueprintConnection->GetDirection() !=
			  outPlan.ExpectedBlueprintDirection ||
		  outPlan.TargetConnection->GetDirection() !=
			  outPlan.ExpectedTargetDirection)))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct preflight rejected reason=direction-changed source=%s actualSource=%d expectedSource=%d target=%s actualTarget=%d expectedTarget=%d"),
			*GetNameSafe(outPlan.BlueprintConnection),
			static_cast<int32>(outPlan.BlueprintConnection->GetDirection()),
			static_cast<int32>(outPlan.ExpectedBlueprintDirection),
			*GetNameSafe(outPlan.TargetConnection),
			static_cast<int32>(outPlan.TargetConnection->GetDirection()),
			static_cast<int32>(outPlan.ExpectedTargetDirection));
		return false;
	}

	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: direct preflight accepted source=%s sourceConnection=%s sourceDirection=%d target=%s targetConnection=%s targetDirection=%d alreadyLinked=%d"),
		*DescribeEndpoint(outPlan.BlueprintEndpoint),
		*GetNameSafe(outPlan.BlueprintConnection),
		static_cast<int32>(outPlan.ExpectedBlueprintDirection),
		*DescribeEndpoint(outPlan.TargetEndpoint),
		*GetNameSafe(outPlan.TargetConnection),
		static_cast<int32>(outPlan.ExpectedTargetDirection),
		outPlan.WasAlreadyLinked ? 1 : 0);
	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::
	ApplyDirectConnectionBookkeeping(FDirectConnectionPlan& plan) const
{
	if (plan.WasAlreadyLinked)
	{
		return true;
	}
	if (!IsValid(plan.BlueprintConnection) ||
		!IsValid(plan.TargetConnection) ||
		GetTransportConnectionAcrossEndpoint(plan.BlueprintEndpoint) !=
			plan.BlueprintConnection ||
		GetTransportConnectionAcrossEndpoint(plan.TargetEndpoint) !=
			plan.TargetConnection)
	{
		return false;
	}

	if (plan.BlueprintConnection->GetDirection() ==
		EFactoryConnectionDirection::FCD_ANY)
	{
		plan.BlueprintConnection->SetDirection(plan.ExpectedBlueprintDirection);
		plan.ChangedBlueprintDirection = true;
	}
	if (plan.TargetConnection->GetDirection() ==
		EFactoryConnectionDirection::FCD_ANY)
	{
		plan.TargetConnection->SetDirection(plan.ExpectedTargetDirection);
		plan.ChangedTargetDirection = true;
	}
	if (plan.BlueprintConnection->GetDirection() !=
			plan.ExpectedBlueprintDirection ||
		plan.TargetConnection->GetDirection() != plan.ExpectedTargetDirection ||
		(!plan.BlueprintConnection->CanConnectTo(plan.TargetConnection) &&
		 !plan.TargetConnection->CanConnectTo(plan.BlueprintConnection)))
	{
		return false;
	}

	auto applyFloorHole = [this](
		const FEndpointRef& endpoint,
		UFGFactoryConnectionComponent* expectedConnection,
		UFGFactoryConnectionComponent* originalConnection,
		bool& outChanged)
	{
		outChanged = false;
		if (endpoint.Kind != EBlueprintVerticalEndpointKind::FloorHoleSide)
		{
			return true;
		}

		AFGBuildablePassthrough* floorHole =
			Cast<AFGBuildablePassthrough>(endpoint.Buildable);
		if (!IsConveyorFloorHole(floorHole) || originalConnection != nullptr ||
			GetFloorHoleSnappedConnection(floorHole, endpoint.Side) !=
				originalConnection)
		{
			return false;
		}

		SetFloorHoleSnappedConnection(
			floorHole,
			endpoint.Side,
			expectedConnection);
		outChanged = true;
		return GetFloorHoleSnappedConnection(floorHole, endpoint.Side) ==
			expectedConnection;
	};

	return applyFloorHole(
			plan.BlueprintEndpoint,
			plan.TargetConnection,
			plan.OriginalBlueprintExposedConnection,
			plan.ChangedBlueprintBookkeeping) &&
		applyFloorHole(
			plan.TargetEndpoint,
			plan.BlueprintConnection,
			plan.OriginalTargetExposedConnection,
			plan.ChangedTargetBookkeeping);
}

bool FBlueprintVerticalConveyorConnectionManager::
	ValidateDirectConnectionPostCondition(
		const FDirectConnectionPlan& plan) const
{
	bool isValid = IsValid(plan.BlueprintConnection) &&
		IsValid(plan.TargetConnection) &&
		plan.BlueprintConnection->GetDirection() ==
			plan.ExpectedBlueprintDirection &&
		plan.TargetConnection->GetDirection() == plan.ExpectedTargetDirection &&
		plan.BlueprintConnection->GetConnection() == plan.TargetConnection &&
		plan.TargetConnection->GetConnection() == plan.BlueprintConnection &&
		plan.BlueprintConnection->IsConnected() &&
		plan.TargetConnection->IsConnected() &&
		GetTransportConnectionAcrossEndpoint(plan.BlueprintEndpoint) ==
			plan.BlueprintConnection &&
		GetTransportConnectionAcrossEndpoint(plan.TargetEndpoint) ==
			plan.TargetConnection;

	if (isValid && plan.BlueprintEndpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		isValid = GetFloorHoleSnappedConnection(
			Cast<AFGBuildablePassthrough>(plan.BlueprintEndpoint.Buildable),
			plan.BlueprintEndpoint.Side) == plan.TargetConnection;
	}
	if (isValid && plan.TargetEndpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		isValid = GetFloorHoleSnappedConnection(
			Cast<AFGBuildablePassthrough>(plan.TargetEndpoint.Buildable),
			plan.TargetEndpoint.Side) == plan.BlueprintConnection;
	}

	if (!isValid)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct post-condition failed source=%s sourceTo=%s sourceConnected=%d target=%s targetTo=%s targetConnected=%d"),
			*GetNameSafe(plan.BlueprintConnection),
			IsValid(plan.BlueprintConnection)
				? *GetNameSafe(plan.BlueprintConnection->GetConnection())
				: TEXT("<invalid>"),
			IsValid(plan.BlueprintConnection) &&
				plan.BlueprintConnection->IsConnected()
				? 1
				: 0,
			*GetNameSafe(plan.TargetConnection),
			IsValid(plan.TargetConnection)
				? *GetNameSafe(plan.TargetConnection->GetConnection())
				: TEXT("<invalid>"),
			IsValid(plan.TargetConnection) &&
				plan.TargetConnection->IsConnected()
				? 1
				: 0);
	}
	return isValid;
}

void FBlueprintVerticalConveyorConnectionManager::RollBackDirectConnection(
	FDirectConnectionPlan& plan) const
{
	if (plan.CreatedLink && IsValid(plan.BlueprintConnection) &&
		IsValid(plan.TargetConnection) &&
		plan.BlueprintConnection->GetConnection() == plan.TargetConnection &&
		plan.TargetConnection->GetConnection() == plan.BlueprintConnection)
	{
		plan.BlueprintConnection->ClearConnection();
	}

	auto restoreFloorHole = [this](
		const FEndpointRef& endpoint,
		UFGFactoryConnectionComponent* expectedCurrent,
		UFGFactoryConnectionComponent* originalConnection,
		bool changed)
	{
		if (!changed || endpoint.Kind !=
			EBlueprintVerticalEndpointKind::FloorHoleSide)
		{
			return;
		}

		AFGBuildablePassthrough* floorHole =
			Cast<AFGBuildablePassthrough>(endpoint.Buildable);
		if (GetFloorHoleSnappedConnection(floorHole, endpoint.Side) ==
			expectedCurrent)
		{
			SetFloorHoleSnappedConnection(
				floorHole,
				endpoint.Side,
				originalConnection);
		}
	};
	restoreFloorHole(
		plan.BlueprintEndpoint,
		plan.TargetConnection,
		plan.OriginalBlueprintExposedConnection,
		plan.ChangedBlueprintBookkeeping);
	restoreFloorHole(
		plan.TargetEndpoint,
		plan.BlueprintConnection,
		plan.OriginalTargetExposedConnection,
		plan.ChangedTargetBookkeeping);

	if (plan.ChangedBlueprintDirection &&
		IsValid(plan.BlueprintConnection) &&
		!plan.BlueprintConnection->IsConnected() &&
		plan.BlueprintConnection->GetConnection() == nullptr)
	{
		plan.BlueprintConnection->SetDirection(plan.OriginalBlueprintDirection);
	}
	if (plan.ChangedTargetDirection && IsValid(plan.TargetConnection) &&
		!plan.TargetConnection->IsConnected() &&
		plan.TargetConnection->GetConnection() == nullptr)
	{
		plan.TargetConnection->SetDirection(plan.OriginalTargetDirection);
	}
}

bool FBlueprintVerticalConveyorConnectionManager::ConnectDirectly(
	const FConnectionState& state) const
{
	FDirectConnectionPlan plan;
	if (!PrepareDirectConnectionPlan(state, plan))
	{
		RollBackDirectConnection(plan);
		return false;
	}

	if (!plan.WasAlreadyLinked)
	{
		if (!ApplyDirectConnectionBookkeeping(plan))
		{
			RollBackDirectConnection(plan);
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: direct finalization rejected reason=bookkeeping-or-capability-changed source=%s target=%s"),
				*DescribeEndpoint(plan.BlueprintEndpoint),
				*DescribeEndpoint(plan.TargetEndpoint));
			return false;
		}

		if (plan.BlueprintConnection->CanConnectTo(plan.TargetConnection))
		{
			plan.BlueprintConnection->SetConnection(plan.TargetConnection);
		}
		else if (plan.TargetConnection->CanConnectTo(plan.BlueprintConnection))
		{
			plan.TargetConnection->SetConnection(plan.BlueprintConnection);
		}
		else
		{
			RollBackDirectConnection(plan);
			return false;
		}
		plan.CreatedLink =
			plan.BlueprintConnection->GetConnection() == plan.TargetConnection &&
			plan.TargetConnection->GetConnection() == plan.BlueprintConnection;
	}

	if (!ValidateDirectConnectionPostCondition(plan))
	{
		RollBackDirectConnection(plan);
		return false;
	}

	RegroupLinkedConveyor(
		Cast<AFGBuildableConveyorBase>(plan.BlueprintConnection->GetOwner()));
	RegroupLinkedConveyor(
		Cast<AFGBuildableConveyorBase>(plan.TargetConnection->GetOwner()));

	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: finalized direct connection source=%s sourceConnection=%s target=%s targetConnection=%s alreadyLinked=%d"),
		*DescribeEndpoint(plan.BlueprintEndpoint),
		*GetNameSafe(plan.BlueprintConnection),
		*DescribeEndpoint(plan.TargetEndpoint),
		*GetNameSafe(plan.TargetConnection),
		plan.WasAlreadyLinked ? 1 : 0);
	ScheduleDirectConnectionValidation(plan);
	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::
	PreflightBridgeEndpointBeforeConstruct(
		const FConnectionState& state,
		const TCHAR* endpointName,
		FBridgeEndpointPlan& endpointPlan) const
{
	const FEndpointRef& endpoint = endpointPlan.Endpoint;
	if (!IsValid(endpoint.Buildable) ||
		(endpointPlan.ExpectedBridgeDirection !=
			EFactoryConnectionDirection::FCD_INPUT &&
		 endpointPlan.ExpectedBridgeDirection !=
			EFactoryConnectionDirection::FCD_OUTPUT))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=invalid-endpoint-or-direction"),
			*DescribeEndpoint(endpoint),
			endpointName);
		return false;
	}

	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		AFGBuildablePassthrough* floorHole =
			Cast<AFGBuildablePassthrough>(endpoint.Buildable);
		if (!IsConveyorFloorHole(floorHole))
		{
			return false;
		}

		UFGFactoryConnectionComponent* exposedConnection =
			GetFloorHoleSnappedConnection(floorHole, endpoint.Side);
		if (exposedConnection != nullptr)
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=exposed-floor-hole-occupied connection=%s"),
				*DescribeEndpoint(endpoint),
				endpointName,
				*GetNameSafe(exposedConnection));
			return false;
		}
	}

	endpointPlan.OutsideConnection =
		GetTransportConnectionAcrossEndpoint(endpoint);
	endpointPlan.HadOutsideConnection =
		endpointPlan.OutsideConnection != nullptr;

	const bool isConstructedBlueprintEndpoint =
		endpoint.Buildable == state.ConstructedBlueprintBuildable;
	if (isConstructedBlueprintEndpoint &&
		endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		if (state.BlueprintContinuationWasPresent)
		{
			if (state.BlueprintContinuationBuildableIndex == INDEX_NONE ||
				!IsValid(
					state.ConstructedBlueprintContinuationConnection) ||
				endpointPlan.OutsideConnection !=
					state.ConstructedBlueprintContinuationConnection)
			{
				UE_LOG(
					LogVerticalConveyorAutoConnect,
					Warning,
					TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=source-continuation-remap-mismatch floorHoleTo=%s remapped=%s sourceIndex=%d"),
					*DescribeEndpoint(endpoint),
					endpointName,
					*GetNameSafe(endpointPlan.OutsideConnection),
					*GetNameSafe(
						state.ConstructedBlueprintContinuationConnection),
					state.BlueprintContinuationBuildableIndex);
				return false;
			}
		}
		else if (endpointPlan.HadOutsideConnection)
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=unexpected-source-continuation floorHoleTo=%s"),
				*DescribeEndpoint(endpoint),
				endpointName,
				*GetNameSafe(endpointPlan.OutsideConnection));
			return false;
		}
	}

	if (!endpointPlan.HadOutsideConnection)
	{
		if (endpoint.Kind ==
			EBlueprintVerticalEndpointKind::AttachmentPort)
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=missing-attachment-port"),
				*DescribeEndpoint(endpoint),
				endpointName);
			return false;
		}

		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: bridge preflight endpoint=%s side=%s terminates=bare"),
			*DescribeEndpoint(endpoint),
			endpointName);
		return true;
	}

	UFGFactoryConnectionComponent* outsideConnection =
		endpointPlan.OutsideConnection;
	if (!IsValid(outsideConnection) ||
		(endpoint.Kind ==
				EBlueprintVerticalEndpointKind::AttachmentPort &&
		 !outsideConnection->IsConnectionVertical()))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=invalid-outside-connection outside=%s"),
			*DescribeEndpoint(endpoint),
			endpointName,
			*GetNameSafe(outsideConnection));
		return false;
	}

	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		AFGBuildableConveyorLift* continuationLift =
			Cast<AFGBuildableConveyorLift>(outsideConnection->GetOwner());
		if (!IsValid(continuationLift) ||
			(outsideConnection != continuationLift->GetConnection0() &&
			 outsideConnection != continuationLift->GetConnection1()))
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=invalid-floor-hole-continuation outside=%s owner=%s"),
				*DescribeEndpoint(endpoint),
				endpointName,
				*GetNameSafe(outsideConnection),
				*GetNameSafe(outsideConnection->GetOwner()));
			return false;
		}
	}

	if (outsideConnection->IsConnected() ||
		outsideConnection->GetConnection() != nullptr)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=outside-already-connected outside=%s outsideTo=%s connected=%d"),
			*DescribeEndpoint(endpoint),
			endpointName,
			*GetNameSafe(outsideConnection),
			*GetNameSafe(outsideConnection->GetConnection()),
			outsideConnection->IsConnected() ? 1 : 0);
		return false;
	}

	const EFactoryConnectionDirection requiredOutsideDirection =
		OppositeDirection(endpointPlan.ExpectedBridgeDirection);
	const EFactoryConnectionDirection outsideDirection =
		outsideConnection->GetDirection();
	if (requiredOutsideDirection == EFactoryConnectionDirection::FCD_ANY ||
		(outsideDirection != EFactoryConnectionDirection::FCD_ANY &&
		 outsideDirection != requiredOutsideDirection))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: bridge preflight rejected endpoint=%s side=%s reason=direction-mismatch bridgeDirection=%d outsideDirection=%d requiredOutsideDirection=%d"),
			*DescribeEndpoint(endpoint),
			endpointName,
			static_cast<int32>(endpointPlan.ExpectedBridgeDirection),
			static_cast<int32>(outsideDirection),
			static_cast<int32>(requiredOutsideDirection));
		return false;
	}

	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::PrepareBridgeFinalizationPlan(
	FConnectionState& state,
	FBridgeFinalizationPlan& outPlan) const
{
	outPlan = {};

	const EFactoryConnectionDirection lowerDirection =
		state.ResolvedLowerEndpointDirection;
	if (lowerDirection != EFactoryConnectionDirection::FCD_INPUT &&
		lowerDirection != EFactoryConnectionDirection::FCD_OUTPUT)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: bridge preflight rejected reason=missing-transport-direction"));
		return false;
	}

	const FEndpointRef blueprintEndpoint = GetBlueprintEndpoint(state, true);
	const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
	if (!IsValid(blueprintEndpoint.Buildable) ||
		!IsValid(targetEndpoint.Buildable))
	{
		return false;
	}

	FEndpointRef lowerEndpoint;
	FEndpointRef upperEndpoint;
	if (GetEndpointWorldTransform(blueprintEndpoint).GetLocation().Z <=
		GetEndpointWorldTransform(targetEndpoint).GetLocation().Z)
	{
		lowerEndpoint = blueprintEndpoint;
		upperEndpoint = targetEndpoint;
	}
	else
	{
		lowerEndpoint = targetEndpoint;
		upperEndpoint = blueprintEndpoint;
	}

	outPlan.ExpectedFlowsUpwards =
		lowerDirection == EFactoryConnectionDirection::FCD_INPUT;
	outPlan.Input.Endpoint = outPlan.ExpectedFlowsUpwards
		? lowerEndpoint
		: upperEndpoint;
	outPlan.Output.Endpoint = outPlan.ExpectedFlowsUpwards
		? upperEndpoint
		: lowerEndpoint;
	outPlan.Input.ExpectedBridgeDirection =
		EFactoryConnectionDirection::FCD_INPUT;
	outPlan.Output.ExpectedBridgeDirection =
		EFactoryConnectionDirection::FCD_OUTPUT;

	if (!PreflightBridgeEndpointBeforeConstruct(
			state,
			TEXT("input"),
			outPlan.Input) ||
		!PreflightBridgeEndpointBeforeConstruct(
			state,
			TEXT("output"),
			outPlan.Output))
	{
		return false;
	}

	if (outPlan.Input.HadOutsideConnection &&
		outPlan.Output.HadOutsideConnection &&
		outPlan.Input.OutsideConnection ==
			outPlan.Output.OutsideConnection)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: bridge preflight rejected reason=shared-outside-connection outside=%s input=%s output=%s"),
			*GetNameSafe(outPlan.Input.OutsideConnection),
			*DescribeEndpoint(outPlan.Input.Endpoint),
			*DescribeEndpoint(outPlan.Output.Endpoint));
		return false;
	}

	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: bridge preflight accepted flow=%s input=%s inputOutside=%s output=%s outputOutside=%s"),
		outPlan.ExpectedFlowsUpwards ? TEXT("up") : TEXT("down"),
		*DescribeEndpoint(outPlan.Input.Endpoint),
		*GetNameSafe(outPlan.Input.OutsideConnection),
		*DescribeEndpoint(outPlan.Output.Endpoint),
		*GetNameSafe(outPlan.Output.OutsideConnection));
	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::
	PrepareConstructedBridgeEndpoint(
		AFGBuildableConveyorLift* lift,
		const TCHAR* endpointName,
		UFGFactoryConnectionComponent* bridgeConnection,
		FBridgeEndpointPlan& endpointPlan) const
{
	endpointPlan.BridgeConnection = bridgeConnection;
	endpointPlan.WasAlreadyLinked = false;
	endpointPlan.ChangedOutsideDirection = false;

	if (!IsValid(bridgeConnection) ||
		bridgeConnection->GetDirection() !=
			endpointPlan.ExpectedBridgeDirection)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s endpoint=%s reason=bridge-direction-mismatch bridge=%s direction=%d expected=%d"),
			*GetNameSafe(lift),
			endpointName,
			*GetNameSafe(bridgeConnection),
			IsValid(bridgeConnection)
				? static_cast<int32>(bridgeConnection->GetDirection())
				: -1,
			static_cast<int32>(endpointPlan.ExpectedBridgeDirection));
		return false;
	}

	UFGFactoryConnectionComponent* currentOutsideConnection =
		GetTransportConnectionAcrossEndpoint(endpointPlan.Endpoint);
	if (currentOutsideConnection != endpointPlan.OutsideConnection ||
		(endpointPlan.HadOutsideConnection &&
		 !IsValid(endpointPlan.OutsideConnection)))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s endpoint=%s reason=outside-connection-changed expected=%s actual=%s"),
			*GetNameSafe(lift),
			endpointName,
			*GetNameSafe(endpointPlan.OutsideConnection),
			*GetNameSafe(currentOutsideConnection));
		return false;
	}

	if (endpointPlan.Endpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		AFGBuildablePassthrough* floorHole =
			Cast<AFGBuildablePassthrough>(endpointPlan.Endpoint.Buildable);
		UFGFactoryConnectionComponent* exposedConnection =
			GetFloorHoleSnappedConnection(
				floorHole,
				endpointPlan.Endpoint.Side);
		if (exposedConnection != nullptr &&
			exposedConnection != bridgeConnection)
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: refusing to finalize %s endpoint=%s reason=floor-hole-changed floorHole=%s exposedTo=%s expected=%s"),
				*GetNameSafe(lift),
				endpointName,
				*GetNameSafe(floorHole),
				*GetNameSafe(exposedConnection),
				*GetNameSafe(bridgeConnection));
			return false;
		}
	}

	UFGFactoryConnectionComponent* bridgePeer =
		bridgeConnection->GetConnection();
	if (!endpointPlan.HadOutsideConnection)
	{
		if (bridgeConnection->IsConnected() || bridgePeer != nullptr)
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: refusing to finalize %s endpoint=%s reason=bare-endpoint-auto-linked bridgeTo=%s connected=%d"),
				*GetNameSafe(lift),
				endpointName,
				*GetNameSafe(bridgePeer),
				bridgeConnection->IsConnected() ? 1 : 0);
			return false;
		}
		return true;
	}

	UFGFactoryConnectionComponent* outsideConnection =
		endpointPlan.OutsideConnection;
	UFGFactoryConnectionComponent* outsidePeer =
		outsideConnection->GetConnection();
	const bool isReciprocal =
		bridgePeer == outsideConnection && outsidePeer == bridgeConnection;
	if (isReciprocal)
	{
		if (!bridgeConnection->IsConnected() ||
			!outsideConnection->IsConnected())
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: refusing to finalize %s endpoint=%s reason=reciprocal-pointer-flag-mismatch bridgeConnected=%d outsideConnected=%d"),
				*GetNameSafe(lift),
				endpointName,
				bridgeConnection->IsConnected() ? 1 : 0,
				outsideConnection->IsConnected() ? 1 : 0);
			return false;
		}
		endpointPlan.WasAlreadyLinked = true;
	}
	else if (bridgeConnection->IsConnected() || bridgePeer != nullptr ||
		outsideConnection->IsConnected() || outsidePeer != nullptr)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s endpoint=%s reason=non-reciprocal-or-conflicting-link bridgeTo=%s bridgeConnected=%d outside=%s outsideTo=%s outsideConnected=%d"),
			*GetNameSafe(lift),
			endpointName,
			*GetNameSafe(bridgePeer),
			bridgeConnection->IsConnected() ? 1 : 0,
			*GetNameSafe(outsideConnection),
			*GetNameSafe(outsidePeer),
			outsideConnection->IsConnected() ? 1 : 0);
		return false;
	}

	endpointPlan.OriginalOutsideDirection =
		outsideConnection->GetDirection();
	const EFactoryConnectionDirection requiredOutsideDirection =
		OppositeDirection(endpointPlan.ExpectedBridgeDirection);
	if (endpointPlan.OriginalOutsideDirection !=
			EFactoryConnectionDirection::FCD_ANY &&
		endpointPlan.OriginalOutsideDirection != requiredOutsideDirection)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s endpoint=%s reason=outside-direction-changed outside=%s direction=%d expected=%d"),
			*GetNameSafe(lift),
			endpointName,
			*GetNameSafe(outsideConnection),
			static_cast<int32>(endpointPlan.OriginalOutsideDirection),
			static_cast<int32>(requiredOutsideDirection));
		return false;
	}

	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::
	ApplyBridgeEndpointBookkeeping(
		AFGBuildableConveyorLift* lift,
		const TCHAR* endpointName,
		FBridgeEndpointPlan& endpointPlan) const
{
	UFGFactoryConnectionComponent* bridgeConnection =
		endpointPlan.BridgeConnection;
	if (!IsValid(bridgeConnection))
	{
		return false;
	}

	if (endpointPlan.HadOutsideConnection)
	{
		UFGFactoryConnectionComponent* outsideConnection =
			endpointPlan.OutsideConnection;
		if (!IsValid(outsideConnection))
		{
			return false;
		}

		const EFactoryConnectionDirection requiredOutsideDirection =
			OppositeDirection(endpointPlan.ExpectedBridgeDirection);
		if (outsideConnection->GetDirection() ==
			EFactoryConnectionDirection::FCD_ANY)
		{
			outsideConnection->SetDirection(requiredOutsideDirection);
			endpointPlan.ChangedOutsideDirection = true;
		}
		if (outsideConnection->GetDirection() != requiredOutsideDirection)
		{
			return false;
		}
	}

	if (endpointPlan.Endpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		AFGBuildablePassthrough* floorHole =
			Cast<AFGBuildablePassthrough>(endpointPlan.Endpoint.Buildable);
		UFGFactoryConnectionComponent* exposedConnection =
			GetFloorHoleSnappedConnection(
				floorHole,
				endpointPlan.Endpoint.Side);
		if (exposedConnection == nullptr)
		{
			SetFloorHoleSnappedConnection(
				floorHole,
				endpointPlan.Endpoint.Side,
				bridgeConnection);
		}
		else if (exposedConnection != bridgeConnection)
		{
			return false;
		}
	}

	if (endpointPlan.HadOutsideConnection &&
		!endpointPlan.WasAlreadyLinked)
	{
		UFGFactoryConnectionComponent* outsideConnection =
			endpointPlan.OutsideConnection;
		if (!bridgeConnection->CanConnectTo(outsideConnection) &&
			!outsideConnection->CanConnectTo(bridgeConnection))
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: refusing to link %s endpoint=%s reason=can-connect-rejected bridge=%s(%d) outside=%s(%d)"),
				*GetNameSafe(lift),
				endpointName,
				*GetNameSafe(bridgeConnection),
				static_cast<int32>(bridgeConnection->GetDirection()),
				*GetNameSafe(outsideConnection),
				static_cast<int32>(outsideConnection->GetDirection()));
			return false;
		}
	}

	return true;
}

int32 FBlueprintVerticalConveyorConnectionManager::ConnectBridgeEndpoint(
	AFGBuildableConveyorLift* lift,
	const TCHAR* endpointName,
	FBridgeEndpointPlan& endpointPlan) const
{
	if (!endpointPlan.HadOutsideConnection)
	{
		return -1;
	}

	UFGFactoryConnectionComponent* bridgeConnection =
		endpointPlan.BridgeConnection;
	UFGFactoryConnectionComponent* outsideConnection =
		endpointPlan.OutsideConnection;
	if (!IsValid(bridgeConnection) || !IsValid(outsideConnection))
	{
		return 0;
	}

	if (endpointPlan.WasAlreadyLinked)
	{
		return bridgeConnection->GetConnection() == outsideConnection &&
			outsideConnection->GetConnection() == bridgeConnection
			? 1
			: 0;
	}

	if (bridgeConnection->CanConnectTo(outsideConnection))
	{
		bridgeConnection->SetConnection(outsideConnection);
	}
	else if (outsideConnection->CanConnectTo(bridgeConnection))
	{
		outsideConnection->SetConnection(bridgeConnection);
	}
	else
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: link changed after preflight lift=%s endpoint=%s bridge=%s outside=%s"),
			*GetNameSafe(lift),
			endpointName,
			*GetNameSafe(bridgeConnection),
			*GetNameSafe(outsideConnection));
		return 0;
	}

	const bool isReciprocal =
		bridgeConnection->GetConnection() == outsideConnection &&
		outsideConnection->GetConnection() == bridgeConnection &&
		bridgeConnection->IsConnected() && outsideConnection->IsConnected();
	return isReciprocal ? 1 : 0;
}

bool FBlueprintVerticalConveyorConnectionManager::
	ValidateBridgeEndpointPostCondition(
		const TCHAR* endpointName,
		const FBridgeEndpointPlan& endpointPlan) const
{
	UFGFactoryConnectionComponent* bridgeConnection =
		endpointPlan.BridgeConnection;
	bool isValid =
		IsValid(bridgeConnection) &&
		bridgeConnection->GetDirection() ==
			endpointPlan.ExpectedBridgeDirection &&
		GetTransportConnectionAcrossEndpoint(endpointPlan.Endpoint) ==
			endpointPlan.OutsideConnection;

	if (isValid && endpointPlan.Endpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		isValid = GetFloorHoleSnappedConnection(
			Cast<AFGBuildablePassthrough>(endpointPlan.Endpoint.Buildable),
			endpointPlan.Endpoint.Side) == bridgeConnection;
	}

	if (isValid && endpointPlan.HadOutsideConnection)
	{
		UFGFactoryConnectionComponent* outsideConnection =
			endpointPlan.OutsideConnection;
		isValid =
			IsValid(outsideConnection) &&
			outsideConnection->GetDirection() ==
				OppositeDirection(endpointPlan.ExpectedBridgeDirection) &&
			bridgeConnection->GetConnection() == outsideConnection &&
			outsideConnection->GetConnection() == bridgeConnection &&
			bridgeConnection->IsConnected() &&
			outsideConnection->IsConnected();
	}
	else if (isValid)
	{
		isValid = !bridgeConnection->IsConnected() &&
			bridgeConnection->GetConnection() == nullptr;
	}

	if (!isValid)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: bridge post-condition failed endpoint=%s bridge=%s bridgeTo=%s outside=%s outsideTo=%s"),
			endpointName,
			*GetNameSafe(bridgeConnection),
			IsValid(bridgeConnection)
				? *GetNameSafe(bridgeConnection->GetConnection())
				: TEXT("<invalid>"),
			*GetNameSafe(endpointPlan.OutsideConnection),
			IsValid(endpointPlan.OutsideConnection)
				? *GetNameSafe(
					endpointPlan.OutsideConnection->GetConnection())
				: TEXT("<none>"));
	}
	return isValid;
}

void FBlueprintVerticalConveyorConnectionManager::RollBackBridgeFinalization(
	AFGBuildableConveyorLift* lift,
	FBridgeFinalizationPlan& plan) const
{
	auto rollBackEndpoint = [this, lift](
		const TCHAR* endpointName,
		FBridgeEndpointPlan& endpointPlan)
	{
		UFGFactoryConnectionComponent* bridgeConnection =
			endpointPlan.BridgeConnection;
		UFGFactoryConnectionComponent* outsideConnection =
			endpointPlan.OutsideConnection;

		if (IsValid(bridgeConnection))
		{
			UFGFactoryConnectionComponent* peer =
				bridgeConnection->GetConnection();
			if (IsValid(peer) && peer->GetConnection() == bridgeConnection)
			{
				bridgeConnection->ClearConnection();
			}
			else if (peer != nullptr || bridgeConnection->IsConnected())
			{
				UE_LOG(
					LogVerticalConveyorAutoConnect,
					Warning,
					TEXT("VerticalConveyorAutoConnect: rollback found a non-reciprocal bridge connection lift=%s endpoint=%s bridge=%s bridgeTo=%s connected=%d"),
					*GetNameSafe(lift),
					endpointName,
					*GetNameSafe(bridgeConnection),
					*GetNameSafe(peer),
					bridgeConnection->IsConnected() ? 1 : 0);
			}
		}

		if (endpointPlan.Endpoint.Kind ==
			EBlueprintVerticalEndpointKind::FloorHoleSide)
		{
			AFGBuildablePassthrough* floorHole =
				Cast<AFGBuildablePassthrough>(endpointPlan.Endpoint.Buildable);
			UFGFactoryConnectionComponent* exposedConnection =
				GetFloorHoleSnappedConnection(
					floorHole,
					endpointPlan.Endpoint.Side);
			if (exposedConnection == bridgeConnection ||
				(IsValid(exposedConnection) &&
				 exposedConnection->GetOwner() == lift))
			{
				SetFloorHoleSnappedConnection(
					floorHole,
					endpointPlan.Endpoint.Side,
					nullptr);
			}
		}

		if (endpointPlan.ChangedOutsideDirection &&
			IsValid(outsideConnection) &&
			!outsideConnection->IsConnected() &&
			outsideConnection->GetConnection() == nullptr)
		{
			outsideConnection->SetDirection(
				endpointPlan.OriginalOutsideDirection);
		}
	};

	rollBackEndpoint(TEXT("input"), plan.Input);
	rollBackEndpoint(TEXT("output"), plan.Output);
	if (IsValid(lift))
	{
		lift->OnRep_SnappedPassthroughs();
	}
}

FBlueprintVerticalConveyorConnectionManager::FBridgeFinalizationResult
FBlueprintVerticalConveyorConnectionManager::FinalizeConstructedBridge(
	FConnectionState& state,
	FBridgeFinalizationPlan& plan,
	AFGBuildableConveyorLift* lift) const
{
	FBridgeFinalizationResult result;
	if (!IsValid(lift))
	{
		result.Status = EBridgeFinalizationStatus::InvalidLift;
		return result;
	}

	UFGFactoryConnectionComponent* connection0 = lift->GetConnection0();
	UFGFactoryConnectionComponent* connection1 = lift->GetConnection1();
	if (!IsValid(connection0) || !IsValid(connection1))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s; constructed lift is missing connection components"),
			*GetNameSafe(lift));
		result.Status = EBridgeFinalizationStatus::LiftStateMismatch;
		return result;
	}

	// Attachment-first vanilla placement can reverse the hologram's physical slot
	// order. Resolve transport identity from the constructed directions instead of
	// assuming GetConnection0() is always input and GetConnection1() output.
	UFGFactoryConnectionComponent* inputConnection = nullptr;
	UFGFactoryConnectionComponent* outputConnection = nullptr;
	if (connection0->GetDirection() == EFactoryConnectionDirection::FCD_INPUT &&
		connection1->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
	{
		inputConnection = connection0;
		outputConnection = connection1;
	}
	else if (connection0->GetDirection() ==
				EFactoryConnectionDirection::FCD_OUTPUT &&
		connection1->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
	{
		inputConnection = connection1;
		outputConnection = connection0;
	}
	else
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s; connection directions are c0=%d c1=%d"),
			*GetNameSafe(lift),
			static_cast<int32>(connection0->GetDirection()),
			static_cast<int32>(connection1->GetDirection()));
		result.Status = EBridgeFinalizationStatus::LiftStateMismatch;
		return result;
	}

	plan.Input.BridgeConnection = inputConnection;
	plan.Output.BridgeConnection = outputConnection;
	const bool actorFlowsUpwards =
		lift->GetConveyorLiftFlowDirection() ==
		EFGBuildableConveyorLiftDirection::LD_Upwards;
	if (state.ResolvedLowerEndpointDirection ==
			EFactoryConnectionDirection::FCD_ANY ||
		actorFlowsUpwards != plan.ExpectedFlowsUpwards)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s expectedFlow=%s actorFlow=%s c0=%d c1=%d"),
			*GetNameSafe(lift),
			plan.ExpectedFlowsUpwards ? TEXT("up") : TEXT("down"),
			actorFlowsUpwards ? TEXT("up") : TEXT("down"),
			static_cast<int32>(connection0->GetDirection()),
			static_cast<int32>(connection1->GetDirection()));
		RollBackBridgeFinalization(lift, plan);
		result.Status = EBridgeFinalizationStatus::LiftStateMismatch;
		return result;
	}

	if (!PrepareConstructedBridgeEndpoint(
			lift,
			TEXT("input"),
			inputConnection,
			plan.Input) ||
		!PrepareConstructedBridgeEndpoint(
			lift,
			TEXT("output"),
			outputConnection,
			plan.Output))
	{
		RollBackBridgeFinalization(lift, plan);
		result.Status = EBridgeFinalizationStatus::EndpointChanged;
		return result;
	}

	// Commit preparation for BOTH endpoints before SetConnection is called on
	// either one. This preserves the vanilla-tested Floor Hole bookkeeping order
	// while preventing a deterministic second-side rejection from leaving the
	// first side linked.
	if (!ApplyBridgeEndpointBookkeeping(
			lift,
			TEXT("input"),
			plan.Input) ||
		!ApplyBridgeEndpointBookkeeping(
			lift,
			TEXT("output"),
			plan.Output))
	{
		RollBackBridgeFinalization(lift, plan);
		result.Status = EBridgeFinalizationStatus::LinkRejected;
		return result;
	}

	result.InputLink = ConnectBridgeEndpoint(
		lift,
		TEXT("input"),
		plan.Input);
	result.OutputLink = ConnectBridgeEndpoint(
		lift,
		TEXT("output"),
		plan.Output);
	if (result.InputLink == 0 || result.OutputLink == 0)
	{
		RollBackBridgeFinalization(lift, plan);
		result.Status = EBridgeFinalizationStatus::LinkRejected;
		return result;
	}

	lift->OnRep_SnappedPassthroughs();
	if (!ValidateBridgeEndpointPostCondition(TEXT("input"), plan.Input) ||
		!ValidateBridgeEndpointPostCondition(TEXT("output"), plan.Output))
	{
		RollBackBridgeFinalization(lift, plan);
		result.Status = EBridgeFinalizationStatus::PostConditionFailed;
		return result;
	}

	result.Status = EBridgeFinalizationStatus::Succeeded;
	RegroupLinkedConveyor(lift);
	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: finalized %s flow=%s input=%s output=%s inputLink=%d outputLink=%d"),
		*GetNameSafe(lift),
		plan.ExpectedFlowsUpwards ? TEXT("up") : TEXT("down"),
		*DescribeEndpoint(plan.Input.Endpoint),
		*DescribeEndpoint(plan.Output.Endpoint),
		result.InputLink,
		result.OutputLink);

	SchedulePostConstructValidation(plan, lift);
	return result;
}

void FBlueprintVerticalConveyorConnectionManager::RegroupLinkedConveyor(
	AFGBuildableConveyorBase* conveyor) const
{
	// AFGBuildableSubsystem::AddConveyor merges a conveyor's bucket with its
	// connected neighbors only when it runs. A conveyor that was registered before
	// it was linked (the generated bridge registers during hologram Construct)
	// keeps a separate bucket and chain actor from an already registered
	// neighbor. The save then stores two chain actors on one connected path,
	// which the next load detects and destroys inside the parallel factory tick.
	// Re-register it like AFGBuildableSubsystem::SplitConveyorGroupFromAttachment
	// does: RemoveConveyor, AddConveyor, then mark the bucket groups dirty.
	if (!IsValid(conveyor) ||
		conveyor->GetConveyorBucketID() == INDEX_NONE)
	{
		return;
	}

	AFGBuildableSubsystem* buildableSubsystem =
		AFGBuildableSubsystem::Get(conveyor);
	if (!IsValid(buildableSubsystem) ||
		!buildableSubsystem->IsServerSubSystem())
	{
		// AddConveyor returns without registering on a non-server subsystem.
		return;
	}

	auto getRegisteredNeighbor = [](
		UFGFactoryConnectionComponent* connection)
		-> AFGBuildableConveyorBase*
	{
		UFGFactoryConnectionComponent* peer =
			IsValid(connection) ? connection->GetConnection() : nullptr;
		AFGBuildableConveyorBase* neighbor = IsValid(peer)
			? Cast<AFGBuildableConveyorBase>(peer->GetOwner())
			: nullptr;
		return IsValid(neighbor) &&
				neighbor->GetConveyorBucketID() != INDEX_NONE
			? neighbor
			: nullptr;
	};
	auto isSplitFrom = [conveyor](AFGBuildableConveyorBase* neighbor)
	{
		return neighbor != nullptr &&
			neighbor->GetConveyorBucketID() != conveyor->GetConveyorBucketID();
	};

	AFGBuildableConveyorBase* neighbor0 =
		getRegisteredNeighbor(conveyor->GetConnection0());
	AFGBuildableConveyorBase* neighbor1 =
		getRegisteredNeighbor(conveyor->GetConnection1());
	if (!isSplitFrom(neighbor0) && !isSplitFrom(neighbor1))
	{
		return;
	}

	const int32 bucketBefore = conveyor->GetConveyorBucketID();
	buildableSubsystem->RemoveConveyor(conveyor);
	buildableSubsystem->AddConveyor(conveyor);
	buildableSubsystem->mConveyorBucketGroupsDirty = true;

	const FString audit = FString::Printf(
		TEXT("%s bucket=%d->%d neighbor0=%s(%d) neighbor1=%s(%d)"),
		*GetNameSafe(conveyor),
		bucketBefore,
		conveyor->GetConveyorBucketID(),
		*GetNameSafe(neighbor0),
		neighbor0 != nullptr ? neighbor0->GetConveyorBucketID() : INDEX_NONE,
		*GetNameSafe(neighbor1),
		neighbor1 != nullptr ? neighbor1->GetConveyorBucketID() : INDEX_NONE);
	if (conveyor->GetConveyorBucketID() == INDEX_NONE ||
		isSplitFrom(neighbor0) || isSplitFrom(neighbor1))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: regroup left split buckets %s"),
			*audit);
	}
	else
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: regrouped %s"),
			*audit);
	}
}

void FBlueprintVerticalConveyorConnectionManager::
	ScheduleDirectConnectionValidation(
		const FDirectConnectionPlan& plan) const
{
	// As with generated bridges, conveyor tick-group inspection is diagnostic and
	// can be proportional to factory size. Only run it for verbose logging.
	if (!UE_LOG_ACTIVE(LogVerticalConveyorAutoConnect, Verbose) ||
		!IsValid(plan.BlueprintConnection) ||
		!IsValid(plan.BlueprintConnection->GetWorld()))
	{
		return;
	}

	auto makeEndpointSnapshot = [](
		const FEndpointRef& endpoint,
		UFGFactoryConnectionComponent* connection,
		EFactoryConnectionDirection expectedDirection)
	{
		FDirectEndpointValidationSnapshot snapshot;
		snapshot.Connection = connection;
		snapshot.ExpectedDirection = expectedDirection;
		if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
		{
			snapshot.HadFloorHole = true;
			snapshot.FloorHole =
				Cast<AFGBuildablePassthrough>(endpoint.Buildable);
			snapshot.FloorHoleSide = endpoint.Side;
		}
		return snapshot;
	};

	FDirectValidationSnapshot snapshot;
	snapshot.Blueprint = makeEndpointSnapshot(
		plan.BlueprintEndpoint,
		plan.BlueprintConnection,
		plan.ExpectedBlueprintDirection);
	snapshot.Target = makeEndpointSnapshot(
		plan.TargetEndpoint,
		plan.TargetConnection,
		plan.ExpectedTargetDirection);

	plan.BlueprintConnection->GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateLambda([snapshot]()
		{
			FBlueprintVerticalConveyorConnectionManager::
				ValidateDirectConnectionNextTick(snapshot);
		}));
}

void FBlueprintVerticalConveyorConnectionManager::
	ValidateDirectConnectionNextTick(FDirectValidationSnapshot snapshot)
{
	UFGFactoryConnectionComponent* blueprintConnection =
		snapshot.Blueprint.Connection.Get();
	UFGFactoryConnectionComponent* targetConnection =
		snapshot.Target.Connection.Get();

	auto validateEndpoint = [](
		const FDirectEndpointValidationSnapshot& endpoint,
		UFGFactoryConnectionComponent* peer)
	{
		UFGFactoryConnectionComponent* connection = endpoint.Connection.Get();
		if (!IsValid(connection) || !IsValid(peer) ||
			connection->GetDirection() != endpoint.ExpectedDirection ||
			connection->GetConnection() != peer || !connection->IsConnected())
		{
			return false;
		}

		if (!endpoint.HadFloorHole)
		{
			return true;
		}

		AFGBuildablePassthrough* floorHole = endpoint.FloorHole.Get();
		return IsValid(floorHole) &&
			FBlueprintVerticalConveyorConnectionManager::
				GetFloorHoleSnappedConnection(
					floorHole,
					endpoint.FloorHoleSide) == peer &&
			FBlueprintVerticalConveyorConnectionManager::
				GetFloorHoleSnappedConnection(
					floorHole,
					FBlueprintVerticalConveyorConnectionManager::OppositeSide(
						endpoint.FloorHoleSide)) == connection;
	};

	const bool topologyValid =
		validateEndpoint(snapshot.Blueprint, targetConnection) &&
		validateEndpoint(snapshot.Target, blueprintConnection) &&
		IsValid(blueprintConnection) && IsValid(targetConnection) &&
		blueprintConnection->GetConnection() == targetConnection &&
		targetConnection->GetConnection() == blueprintConnection;

	AFGBuildableSubsystem* buildableSubsystem =
		IsValid(blueprintConnection)
			? AFGBuildableSubsystem::Get(blueprintConnection->GetOwner())
			: nullptr;
	struct FConveyorMembership
	{
		AFGBuildableConveyorBase* Conveyor = nullptr;
		AFGConveyorChainActor* SavedChainActor = nullptr;
		AFGConveyorChainActor* TickGroupChainActor = nullptr;
		int32 MatchingTickGroups = 0;
		bool MatchedGroupIsPending = false;
		bool IsMismatch = false;
	};

	auto inspectConveyor = [buildableSubsystem](
		UFGFactoryConnectionComponent* connection)
	{
		FConveyorMembership result;
		result.Conveyor = IsValid(connection)
			? Cast<AFGBuildableConveyorBase>(connection->GetOwner())
			: nullptr;
		if (!IsValid(result.Conveyor))
		{
			return result;
		}

		result.SavedChainActor = result.Conveyor->GetConveyorChainActor();
		if (IsValid(buildableSubsystem))
		{
			for (FConveyorTickGroup* tickGroup :
				buildableSubsystem->mConveyorTickGroup)
			{
				if (tickGroup == nullptr ||
					!tickGroup->Conveyors.ContainsByPredicate(
						[&result](
							const TObjectPtr<AFGBuildableConveyorBase>& conveyor)
						{
							return conveyor.Get() == result.Conveyor;
						}))
				{
					continue;
				}

				++result.MatchingTickGroups;
				if (result.MatchingTickGroups == 1)
				{
					result.TickGroupChainActor = tickGroup->ChainActor;
				}
				result.MatchedGroupIsPending =
					result.MatchedGroupIsPending ||
					buildableSubsystem->mConveyorGroupsPendingChainActors.Contains(
						tickGroup);
			}
		}

		result.IsMismatch = result.MatchingTickGroups != 1 ||
			(!result.MatchedGroupIsPending &&
			 result.TickGroupChainActor != result.SavedChainActor);
		return result;
	};

	const FConveyorMembership blueprintMembership =
		inspectConveyor(blueprintConnection);
	const FConveyorMembership targetMembership =
		inspectConveyor(targetConnection);
	const bool chainMismatch = blueprintMembership.IsMismatch ||
		targetMembership.IsMismatch ||
		(IsValid(blueprintMembership.Conveyor) &&
		 IsValid(targetMembership.Conveyor) &&
		 blueprintMembership.Conveyor->GetConveyorBucketID() !=
			targetMembership.Conveyor->GetConveyorBucketID());

	const FString audit = FString::Printf(
		TEXT("topology=%d source=%s sourceOwner=%s sourceBucket=%d sourceSavedChain=%s sourceTickGroups=%d sourceTickChain=%s sourceTickPending=%d target=%s targetOwner=%s targetBucket=%d targetSavedChain=%s targetTickGroups=%d targetTickChain=%s targetTickPending=%d"),
		topologyValid ? 1 : 0,
		*GetNameSafe(blueprintConnection),
		*GetNameSafe(blueprintMembership.Conveyor),
		IsValid(blueprintMembership.Conveyor)
			? blueprintMembership.Conveyor->GetConveyorBucketID()
			: INDEX_NONE,
		*GetNameSafe(blueprintMembership.SavedChainActor),
		blueprintMembership.MatchingTickGroups,
		*GetNameSafe(blueprintMembership.TickGroupChainActor),
		blueprintMembership.MatchedGroupIsPending ? 1 : 0,
		*GetNameSafe(targetConnection),
		*GetNameSafe(targetMembership.Conveyor),
		IsValid(targetMembership.Conveyor)
			? targetMembership.Conveyor->GetConveyorBucketID()
			: INDEX_NONE,
		*GetNameSafe(targetMembership.SavedChainActor),
		targetMembership.MatchingTickGroups,
		*GetNameSafe(targetMembership.TickGroupChainActor),
		targetMembership.MatchedGroupIsPending ? 1 : 0);

	if (!topologyValid || chainMismatch)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: direct post-construct audit failed %s"),
			*audit);
	}
	else
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: direct post-construct audit passed %s"),
			*audit);
	}
}

void FBlueprintVerticalConveyorConnectionManager::
	SchedulePostConstructValidation(
		const FBridgeFinalizationPlan& plan,
		AFGBuildableConveyorLift* lift) const
{
	// The tick-group scan is diagnostic and can be proportional to factory size.
	// Keep it out of normal gameplay unless detailed VCA logging is enabled.
	if (!UE_LOG_ACTIVE(LogVerticalConveyorAutoConnect, Verbose) ||
		!IsValid(lift) || !IsValid(lift->GetWorld()))
	{
		return;
	}

	auto makeEndpointSnapshot = [](
		const FBridgeEndpointPlan& endpointPlan)
	{
		FBridgeEndpointValidationSnapshot snapshot;
		snapshot.BridgeConnection = endpointPlan.BridgeConnection;
		snapshot.OutsideConnection = endpointPlan.OutsideConnection;
		snapshot.ExpectedBridgeDirection =
			endpointPlan.ExpectedBridgeDirection;
		snapshot.HadOutsideConnection =
			endpointPlan.HadOutsideConnection;
		if (endpointPlan.Endpoint.Kind ==
			EBlueprintVerticalEndpointKind::FloorHoleSide)
		{
			snapshot.HadFloorHole = true;
			snapshot.FloorHole = Cast<AFGBuildablePassthrough>(
				endpointPlan.Endpoint.Buildable);
			snapshot.FloorHoleSide = endpointPlan.Endpoint.Side;
		}
		return snapshot;
	};

	FBridgeValidationSnapshot snapshot;
	snapshot.Lift = lift;
	snapshot.Input = makeEndpointSnapshot(plan.Input);
	snapshot.Output = makeEndpointSnapshot(plan.Output);

	lift->GetWorldTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateLambda([snapshot]()
		{
			FBlueprintVerticalConveyorConnectionManager::
				ValidateConstructedBridgeNextTick(snapshot);
		}));
}

void FBlueprintVerticalConveyorConnectionManager::
	ValidateConstructedBridgeNextTick(FBridgeValidationSnapshot snapshot)
{
	AFGBuildableConveyorLift* lift = snapshot.Lift.Get();
	if (!IsValid(lift))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: post-construct audit failed reason=lift-invalid"));
		return;
	}

	auto validateEndpoint = [](
		const FBridgeEndpointValidationSnapshot& endpoint)
	{
		UFGFactoryConnectionComponent* bridgeConnection =
			endpoint.BridgeConnection.Get();
		UFGFactoryConnectionComponent* outsideConnection =
			endpoint.OutsideConnection.Get();
		if (!IsValid(bridgeConnection) ||
			bridgeConnection->GetDirection() !=
				endpoint.ExpectedBridgeDirection ||
			(endpoint.HadOutsideConnection &&
			 !IsValid(outsideConnection)))
		{
			return false;
		}

		if (endpoint.HadFloorHole)
		{
			AFGBuildablePassthrough* floorHole = endpoint.FloorHole.Get();
			if (!IsValid(floorHole) ||
				FBlueprintVerticalConveyorConnectionManager::
					GetFloorHoleSnappedConnection(
						floorHole,
						endpoint.FloorHoleSide) != bridgeConnection ||
				FBlueprintVerticalConveyorConnectionManager::
					GetFloorHoleSnappedConnection(
						floorHole,
						FBlueprintVerticalConveyorConnectionManager::
							OppositeSide(endpoint.FloorHoleSide)) !=
						(endpoint.HadOutsideConnection
							? outsideConnection
							: nullptr))
			{
				return false;
			}
		}

		if (!endpoint.HadOutsideConnection)
		{
			return !bridgeConnection->IsConnected() &&
				bridgeConnection->GetConnection() == nullptr;
		}

		return IsValid(outsideConnection) &&
			outsideConnection->GetDirection() ==
				FBlueprintVerticalConveyorConnectionManager::
					OppositeDirection(endpoint.ExpectedBridgeDirection) &&
			bridgeConnection->GetConnection() == outsideConnection &&
			outsideConnection->GetConnection() == bridgeConnection &&
			bridgeConnection->IsConnected() &&
			outsideConnection->IsConnected();
	};

	const bool topologyValid =
		validateEndpoint(snapshot.Input) &&
		validateEndpoint(snapshot.Output);

	AFGBuildableSubsystem* buildableSubsystem =
		AFGBuildableSubsystem::Get(lift);
	int32 matchingTickGroups = 0;
	bool matchedGroupIsPending = false;
	AFGConveyorChainActor* tickGroupChainActor = nullptr;
	if (IsValid(buildableSubsystem))
	{
		for (FConveyorTickGroup* tickGroup :
			buildableSubsystem->mConveyorTickGroup)
		{
			if (tickGroup == nullptr ||
				!tickGroup->Conveyors.ContainsByPredicate(
					[lift](
						const TObjectPtr<AFGBuildableConveyorBase>&
							conveyor)
					{
						return conveyor.Get() == lift;
					}))
			{
				continue;
			}

			++matchingTickGroups;
			if (matchingTickGroups == 1)
			{
				tickGroupChainActor = tickGroup->ChainActor;
			}
			matchedGroupIsPending = matchedGroupIsPending ||
				buildableSubsystem->mConveyorGroupsPendingChainActors.Contains(
					tickGroup);
		}
	}

	AFGConveyorChainActor* savedChainActor =
		lift->GetConveyorChainActor();
	const bool tickGroupMismatch = matchingTickGroups != 1 ||
		(!matchedGroupIsPending &&
		 tickGroupChainActor != savedChainActor);

	auto getOutsideConveyor = [](
		const FBridgeEndpointValidationSnapshot& endpoint)
		-> AFGBuildableConveyorBase*
	{
		UFGFactoryConnectionComponent* outsideConnection =
			endpoint.OutsideConnection.Get();
		return endpoint.HadOutsideConnection && IsValid(outsideConnection)
			? Cast<AFGBuildableConveyorBase>(outsideConnection->GetOwner())
			: nullptr;
	};
	AFGBuildableConveyorBase* inputConveyor =
		getOutsideConveyor(snapshot.Input);
	AFGBuildableConveyorBase* outputConveyor =
		getOutsideConveyor(snapshot.Output);
	auto isSplitFromLift = [lift](AFGBuildableConveyorBase* neighbor)
	{
		return IsValid(neighbor) &&
			neighbor->GetConveyorBucketID() != lift->GetConveyorBucketID();
	};
	const bool neighborBucketMismatch =
		isSplitFromLift(inputConveyor) || isSplitFromLift(outputConveyor);

	const FString audit = FString::Printf(
		TEXT("lift=%s topology=%d bucket=%d savedChain=%s tickGroups=%d tickChain=%s tickPending=%d inputNeighbor=%s inputBucket=%d inputChain=%s outputNeighbor=%s outputBucket=%d outputChain=%s"),
		*GetNameSafe(lift),
		topologyValid ? 1 : 0,
		lift->GetConveyorBucketID(),
		*GetNameSafe(savedChainActor),
		matchingTickGroups,
		*GetNameSafe(tickGroupChainActor),
		matchedGroupIsPending ? 1 : 0,
		*GetNameSafe(inputConveyor),
		IsValid(inputConveyor) ? inputConveyor->GetConveyorBucketID() : INDEX_NONE,
		IsValid(inputConveyor)
			? *GetNameSafe(inputConveyor->GetConveyorChainActor())
			: TEXT("<none>"),
		*GetNameSafe(outputConveyor),
		IsValid(outputConveyor) ? outputConveyor->GetConveyorBucketID() : INDEX_NONE,
		IsValid(outputConveyor)
			? *GetNameSafe(outputConveyor->GetConveyorChainActor())
			: TEXT("<none>"));

	if (!topologyValid || tickGroupMismatch || neighborBucketMismatch)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: post-construct audit failed %s"),
			*audit);
	}
	else
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: post-construct audit passed %s"),
			*audit);
	}
}

void FBlueprintVerticalConveyorConnectionManager::Construct(
	TArray<AFGBuildable*>& outConstructedBridgeBuildables,
	FNetConstructionID netConstructionID)
{
	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: construct begin states=%d"),
		ConnectionStates.Num());
	const double startSeconds = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: construct end bridges=%d in %.1f ms"),
			outConstructedBridgeBuildables.Num(),
			(FPlatformTime::Seconds() - startSeconds) * 1000.0);
	};

	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		if (!state.IsValid ||
			!IsValid(state.TargetBuildable) ||
			!IsValid(state.ConstructedBlueprintBuildable))
		{
			continue;
		}

		if (state.CanDirectlyConnect)
		{
			if (!ConnectDirectly(state))
			{
				UE_LOG(
					LogVerticalConveyorAutoConnect,
					Error,
					TEXT("VerticalConveyorAutoConnect: direct finalization failed source=%s target=%s"),
					*DescribeEndpoint(GetBlueprintEndpoint(state, true)),
					*DescribeEndpoint(GetTargetEndpoint(state)));
			}
			continue;
		}


		if (!ConfigureBridgeHologram(state, true, true) ||
			!IsValid(state.BridgeHologram) ||
			state.BridgeHologram->IsDisabled())
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: final bridge preparation failed for %s -> %s"),
				*DescribeEndpoint(GetBlueprintEndpoint(state, true)),
				*DescribeEndpoint(GetTargetEndpoint(state)));
			continue;
		}

		FBridgeFinalizationPlan finalizationPlan;
		if (!PrepareBridgeFinalizationPlan(state, finalizationPlan))
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: final bridge preflight failed for %s -> %s; bridge was not constructed"),
				*DescribeEndpoint(GetBlueprintEndpoint(state, true)),
				*DescribeEndpoint(GetTargetEndpoint(state)));
			continue;
		}


		TArray<AActor*> constructedChildren;
		AFGBuildableConveyorLift* lift = Cast<AFGBuildableConveyorLift>(
			state.BridgeHologram->Construct(
				constructedChildren,
				netConstructionID));
		if (!IsValid(lift))
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: bridge construction failed for %s -> %s"),
				*DescribeEndpoint(GetBlueprintEndpoint(state, true)),
				*DescribeEndpoint(GetTargetEndpoint(state)));
			continue;
		}

		const FBridgeFinalizationResult finalizationResult =
			FinalizeConstructedBridge(
				state,
				finalizationPlan,
				lift);
		if (!finalizationResult.IsSuccess())
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Error,
				TEXT("VerticalConveyorAutoConnect: discarding failed bridge %s status=%d inputLink=%d outputLink=%d"),
				*GetNameSafe(lift),
				static_cast<int32>(finalizationResult.Status),
				finalizationResult.InputLink,
				finalizationResult.OutputLink);

			// FinalizeConstructedBridge rolls back every mutation path it reaches.
			// Repeat the idempotent cleanup here as a last guard for failures that
			// occurred before the constructed connection slots could be resolved.
			RollBackBridgeFinalization(lift, finalizationPlan);
			for (AActor* child : constructedChildren)
			{
				if (IsValid(child))
				{
					child->Destroy();
				}
			}
			lift->Destroy();
			continue;
		}

		outConstructedBridgeBuildables.Add(lift);
		for (AActor* child : constructedChildren)
		{
			if (AFGBuildable* childBuildable = Cast<AFGBuildable>(child))
			{
				outConstructedBridgeBuildables.Add(childBuildable);
			}
		}
	}
}
