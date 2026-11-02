#include "SeamLinker.h"

#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "ConveyorLiftAutoConnectModule.h"
#include "FGBuildableSubsystem.h"
#include "SeamAudit.h"
#include "SeamSearch.h"

namespace
{
	bool Reject(const TCHAR* Seam, const TCHAR* Reason, const FString& Context)
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("%s rejected (%s): %s"), Seam, Reason, *Context);
		return false;
	}

	bool IsLinkedPair(const UFGFactoryConnectionComponent* First, const UFGFactoryConnectionComponent* Second)
	{
		return IsValid(First) && IsValid(Second) &&
			First->GetConnection() == Second && Second->GetConnection() == First &&
			First->IsConnected() && Second->IsConnected();
	}

	bool IsUnlinked(const UFGFactoryConnectionComponent* Connection)
	{
		return Connection->GetConnection() == nullptr && !Connection->IsConnected();
	}

	AFGBuildableConveyorBase* GetConveyor(const UFGFactoryConnectionComponent* Connection)
	{
		return IsValid(Connection) ? Cast<AFGBuildableConveyorBase>(Connection->GetOwner()) : nullptr;
	}
}

void FSeamTransaction::SetDirection(UFGFactoryConnectionComponent* Connection, EFactoryConnectionDirection Direction)
{
	const EFactoryConnectionDirection Previous = Connection->GetDirection();
	Connection->SetDirection(Direction);
	UndoSteps.Add([WeakConnection = TWeakObjectPtr<UFGFactoryConnectionComponent>(Connection), Previous]()
	{
		UFGFactoryConnectionComponent* Connection = WeakConnection.Get();
		if (IsValid(Connection) && IsUnlinked(Connection))
		{
			Connection->SetDirection(Previous);
		}
	});
}

void FSeamTransaction::SetFloorHoleSnapped(AFGBuildablePassthrough* Hole, ELiftFacing Side, UFGFactoryConnectionComponent* Connection)
{
	UFGFactoryConnectionComponent* Previous = LiftAutoConnect::GetFloorHoleSnapped(Hole, Side);
	LiftAutoConnect::SetFloorHoleSnapped(Hole, Side, Connection);
	UndoSteps.Add([WeakHole = TWeakObjectPtr<AFGBuildablePassthrough>(Hole), Side, Connection, Previous]()
	{
		AFGBuildablePassthrough* Hole = WeakHole.Get();
		if (IsValid(Hole) && LiftAutoConnect::GetFloorHoleSnapped(Hole, Side) == Connection)
		{
			LiftAutoConnect::SetFloorHoleSnapped(Hole, Side, Previous);
		}
	});
}

bool FSeamTransaction::Link(UFGFactoryConnectionComponent* First, UFGFactoryConnectionComponent* Second)
{
	if (First->CanConnectTo(Second))
	{
		First->SetConnection(Second);
	}
	else if (Second->CanConnectTo(First))
	{
		Second->SetConnection(First);
	}
	else
	{
		return false;
	}
	UndoSteps.Add([WeakFirst = TWeakObjectPtr<UFGFactoryConnectionComponent>(First),
		WeakSecond = TWeakObjectPtr<UFGFactoryConnectionComponent>(Second)]()
	{
		UFGFactoryConnectionComponent* First = WeakFirst.Get();
		UFGFactoryConnectionComponent* Second = WeakSecond.Get();
		if (IsValid(First) && IsValid(Second) && First->GetConnection() == Second && Second->GetConnection() == First)
		{
			First->ClearConnection();
		}
	});
	return First->GetConnection() == Second && Second->GetConnection() == First;
}

void FSeamTransaction::Rollback()
{
	for (int32 Index = UndoSteps.Num() - 1; Index >= 0; --Index)
	{
		UndoSteps[Index]();
	}
	UndoSteps.Reset();
}

bool FSeamLinker::RestoreSavedAttachmentDirection(
	const FLiftEndpoint& Port,
	EFactoryConnectionDirection Expected,
	FSeamTransaction* Transaction)
{
	AFGBuildableConveyorAttachment* Attachment = Cast<AFGBuildableConveyorAttachment>(Port.Buildable);
	UFGFactoryConnectionComponent* Connection = Port.GetPort();
	if (!IsValid(Attachment) || !IsValid(Connection) || !LiftAutoConnect::IsTransportDirection(Expected))
	{
		return false;
	}

	// mSavedDirections follows the order of SortComponentList.
	TInlineComponentArray<UFGFactoryConnectionComponent*> Ordered;
	Attachment->GetComponents(Ordered);
	UFGFactoryConnectionComponent::SortComponentList(Ordered);
	const int32 Index = Ordered.IndexOfByKey(Connection);
	if (Index == INDEX_NONE || Ordered.Num() != Attachment->mSavedDirections.Num())
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning,
			TEXT("No saved direction for %s (index %d, %d components, %d saved)"),
			*Port.Describe(), Index, Ordered.Num(), Attachment->mSavedDirections.Num());
		return false;
	}

	const EFactoryConnectionDirection Saved = Attachment->mSavedDirections[Index];
	if (Saved != Expected)
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning,
			TEXT("Saved direction %d of %s disagrees with the previewed flow (%d)"),
			static_cast<int32>(Saved), *Port.Describe(), static_cast<int32>(Expected));
		return false;
	}

	if (Connection->GetDirection() != Saved)
	{
		if (Transaction)
		{
			Transaction->SetDirection(Connection, Saved);
		}
		else
		{
			Connection->SetDirection(Saved);
		}
	}
	return true;
}

bool FSeamLinker::LinkDirect(const FDirectSeam& Seam)
{
	const FLiftEndpoint& Blueprint = Seam.Blueprint;
	const FLiftEndpoint& Target = Seam.Target;
	const FString Context = FString::Printf(TEXT("%s -> %s"), *Blueprint.Describe(), *Target.Describe());
	auto Fail = [&Context](const TCHAR* Reason) { return Reject(TEXT("Direct seam"), Reason, Context); };

	if (!Blueprint.HasBuildable() || !Target.HasBuildable() ||
		!LiftAutoConnect::IsTransportDirection(Seam.BlueprintDirection) ||
		LiftAutoConnect::Opposite(Seam.BlueprintDirection) != Seam.TargetDirection)
	{
		return Fail(TEXT("invalid endpoint or direction"));
	}
	if (SeamSearch::Classify(Blueprint, Blueprint.GetLocation(), Target, Target.GetLocation()) != ESeamGeometry::Direct)
	{
		return Fail(TEXT("endpoints no longer touch"));
	}

	UFGFactoryConnectionComponent* BlueprintConnection = Blueprint.GetContinuation();
	UFGFactoryConnectionComponent* TargetConnection = Target.GetContinuation();
	if (!IsValid(BlueprintConnection) || !IsValid(TargetConnection) || BlueprintConnection == TargetConnection)
	{
		return Fail(TEXT("missing or shared connection"));
	}
	for (const FLiftEndpoint* Endpoint : {&Blueprint, &Target})
	{
		if (Endpoint->Kind == ELiftEndpointKind::FloorHole &&
			(!LiftAutoConnect::IsConveyorFloorHole(Endpoint->GetFloorHole()) || !IsLiftConnection(Endpoint->GetContinuation())))
		{
			return Fail(TEXT("floor hole continuation is not a lift"));
		}
	}
	if (Blueprint.Kind == ELiftEndpointKind::FloorHole &&
		(!Seam.Continuation.bPresent || Seam.Continuation.Constructed != BlueprintConnection))
	{
		return Fail(TEXT("blueprint continuation was not remapped"));
	}

	const bool bLinked = BlueprintConnection->GetConnection() == TargetConnection &&
		TargetConnection->GetConnection() == BlueprintConnection;
	if (bLinked && !IsLinkedPair(BlueprintConnection, TargetConnection))
	{
		return Fail(TEXT("link flags disagree"));
	}
	if (!bLinked && (!IsUnlinked(BlueprintConnection) || !IsUnlinked(TargetConnection)))
	{
		return Fail(TEXT("connection is linked elsewhere"));
	}
	auto HoleSideHolds = [bLinked](const FLiftEndpoint& Endpoint, UFGFactoryConnectionComponent* Other)
	{
		return Endpoint.Kind != ELiftEndpointKind::FloorHole ||
			LiftAutoConnect::GetFloorHoleSnapped(Endpoint.GetFloorHole(), Endpoint.Facing) == (bLinked ? Other : nullptr);
	};
	if (!HoleSideHolds(Blueprint, TargetConnection) || !HoleSideHolds(Target, BlueprintConnection))
	{
		return Fail(TEXT("floor hole side changed"));
	}

	FSeamTransaction Transaction;
	auto Abort = [&Transaction, &Fail](const TCHAR* Reason)
	{
		Transaction.Rollback();
		return Fail(Reason);
	};

	if (!bLinked && Blueprint.Kind == ELiftEndpointKind::AttachmentPort &&
		!RestoreSavedAttachmentDirection(Blueprint, Seam.BlueprintDirection, &Transaction))
	{
		return Abort(TEXT("attachment saved direction"));
	}
	auto DirectionFits = [bLinked](EFactoryConnectionDirection Actual, EFactoryConnectionDirection Expected)
	{
		return Actual == Expected || (!bLinked && Actual == EFactoryConnectionDirection::FCD_ANY);
	};
	if (!DirectionFits(BlueprintConnection->GetDirection(), Seam.BlueprintDirection) ||
		!DirectionFits(TargetConnection->GetDirection(), Seam.TargetDirection))
	{
		return Abort(TEXT("direction changed"));
	}

	if (!bLinked)
	{
		if (BlueprintConnection->GetDirection() == EFactoryConnectionDirection::FCD_ANY)
		{
			Transaction.SetDirection(BlueprintConnection, Seam.BlueprintDirection);
		}
		if (TargetConnection->GetDirection() == EFactoryConnectionDirection::FCD_ANY)
		{
			Transaction.SetDirection(TargetConnection, Seam.TargetDirection);
		}
		if (!BlueprintConnection->CanConnectTo(TargetConnection) && !TargetConnection->CanConnectTo(BlueprintConnection))
		{
			return Abort(TEXT("link refused"));
		}
		if (Blueprint.Kind == ELiftEndpointKind::FloorHole)
		{
			Transaction.SetFloorHoleSnapped(Blueprint.GetFloorHole(), Blueprint.Facing, TargetConnection);
		}
		if (Target.Kind == ELiftEndpointKind::FloorHole)
		{
			Transaction.SetFloorHoleSnapped(Target.GetFloorHole(), Target.Facing, BlueprintConnection);
		}
		if (!Transaction.Link(BlueprintConnection, TargetConnection))
		{
			return Abort(TEXT("link refused"));
		}
	}

	auto HoleSideLinked = [](const FLiftEndpoint& Endpoint, UFGFactoryConnectionComponent* Other)
	{
		return Endpoint.Kind != ELiftEndpointKind::FloorHole ||
			LiftAutoConnect::GetFloorHoleSnapped(Endpoint.GetFloorHole(), Endpoint.Facing) == Other;
	};
	if (BlueprintConnection->GetDirection() != Seam.BlueprintDirection ||
		TargetConnection->GetDirection() != Seam.TargetDirection ||
		!IsLinkedPair(BlueprintConnection, TargetConnection) ||
		Blueprint.GetContinuation() != BlueprintConnection || Target.GetContinuation() != TargetConnection ||
		!HoleSideLinked(Blueprint, TargetConnection) || !HoleSideLinked(Target, BlueprintConnection))
	{
		return Abort(TEXT("links did not hold"));
	}

	Regroup(GetConveyor(BlueprintConnection));
	Regroup(GetConveyor(TargetConnection));
	UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Linked direct seam %s (already linked: %d)"), *Context, bLinked ? 1 : 0);

	auto MakeAuditLink = [](const FLiftEndpoint& Endpoint, UFGFactoryConnectionComponent* Connection,
		UFGFactoryConnectionComponent* Peer, EFactoryConnectionDirection Direction)
	{
		FSeamAuditLink Link;
		Link.Connection = Connection;
		Link.Peer = Peer;
		Link.Direction = Direction;
		Link.FloorHole = Endpoint.GetFloorHole();
		Link.FloorHoleSide = Endpoint.Facing;
		Link.SideSnapped = Peer;
		Link.OppositeSnapped = Connection;
		return Link;
	};
	FSeamAudit::Schedule(BlueprintConnection->GetWorld(), Context,
		{MakeAuditLink(Blueprint, BlueprintConnection, TargetConnection, Seam.BlueprintDirection),
		 MakeAuditLink(Target, TargetConnection, BlueprintConnection, Seam.TargetDirection)},
		{GetConveyor(BlueprintConnection), GetConveyor(TargetConnection)});
	return true;
}

bool FSeamLinker::PlanBridge(const FBridgeSeam& Seam, FBridgePlan& OutPlan)
{
	OutPlan = {};
	const FString Context = FString::Printf(TEXT("%s -> %s"), *Seam.Blueprint.Describe(), *Seam.Target.Describe());
	if (Seam.Flow == ELiftFlow::Unknown || !Seam.Blueprint.HasBuildable() || !Seam.Target.HasBuildable())
	{
		return Reject(TEXT("Bridge"), TEXT("invalid endpoint or flow"), Context);
	}

	const bool bBlueprintLower = Seam.Blueprint.GetLocation().Z <= Seam.Target.GetLocation().Z;
	const FLiftEndpoint& Lower = bBlueprintLower ? Seam.Blueprint : Seam.Target;
	const FLiftEndpoint& Upper = bBlueprintLower ? Seam.Target : Seam.Blueprint;
	OutPlan.Flow = Seam.Flow;
	OutPlan.Input.Endpoint = Seam.Flow == ELiftFlow::Up ? Lower : Upper;
	OutPlan.Input.LiftDirection = EFactoryConnectionDirection::FCD_INPUT;
	OutPlan.Output.Endpoint = Seam.Flow == ELiftFlow::Up ? Upper : Lower;
	OutPlan.Output.LiftDirection = EFactoryConnectionDirection::FCD_OUTPUT;

	if (!CheckBridgeEnd(Seam, TEXT("input"), OutPlan.Input) || !CheckBridgeEnd(Seam, TEXT("output"), OutPlan.Output))
	{
		return false;
	}
	if (OutPlan.Input.Outside != nullptr && OutPlan.Input.Outside == OutPlan.Output.Outside)
	{
		return Reject(TEXT("Bridge"), TEXT("both ends share one connection"), Context);
	}

	UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Bridge planned flow=%s input=%s (%s) output=%s (%s)"),
		LiftAutoConnect::ToString(OutPlan.Flow),
		*OutPlan.Input.Endpoint.Describe(), *GetNameSafe(OutPlan.Input.Outside),
		*OutPlan.Output.Endpoint.Describe(), *GetNameSafe(OutPlan.Output.Outside));
	return true;
}

bool FSeamLinker::CheckBridgeEnd(const FBridgeSeam& Seam, const TCHAR* Name, FBridgeEnd& End)
{
	const FLiftEndpoint& Endpoint = End.Endpoint;
	const FString Context = FString::Printf(TEXT("%s end %s"), Name, *Endpoint.Describe());
	auto Fail = [&Context](const TCHAR* Reason) { return Reject(TEXT("Bridge"), Reason, Context); };

	if (!Endpoint.HasBuildable() || !LiftAutoConnect::IsTransportDirection(End.LiftDirection))
	{
		return Fail(TEXT("invalid endpoint or direction"));
	}
	if (Endpoint.Kind == ELiftEndpointKind::FloorHole)
	{
		AFGBuildablePassthrough* Hole = Endpoint.GetFloorHole();
		if (!LiftAutoConnect::IsConveyorFloorHole(Hole))
		{
			return Fail(TEXT("not a conveyor floor hole"));
		}
		if (LiftAutoConnect::GetFloorHoleSnapped(Hole, Endpoint.Facing) != nullptr)
		{
			return Fail(TEXT("floor hole side is taken"));
		}
	}

	End.Outside = Endpoint.GetContinuation();
	if (Endpoint.Buildable == Seam.Blueprint.Buildable && Endpoint.Kind == ELiftEndpointKind::FloorHole)
	{
		if (Seam.Continuation.bPresent && (Seam.Continuation.Constructed == nullptr || End.Outside != Seam.Continuation.Constructed))
		{
			return Fail(TEXT("blueprint continuation was not remapped"));
		}
		if (!Seam.Continuation.bPresent && End.Outside != nullptr)
		{
			return Fail(TEXT("unexpected blueprint continuation"));
		}
	}

	if (End.Outside == nullptr)
	{
		return Endpoint.Kind == ELiftEndpointKind::FloorHole || Fail(TEXT("attachment port is missing"));
	}
	if (Endpoint.Kind == ELiftEndpointKind::AttachmentPort && !End.Outside->IsConnectionVertical())
	{
		return Fail(TEXT("attachment port is not vertical"));
	}
	if (Endpoint.Kind == ELiftEndpointKind::FloorHole && !IsLiftConnection(End.Outside))
	{
		return Fail(TEXT("floor hole continuation is not a lift"));
	}
	if (!IsUnlinked(End.Outside))
	{
		return Fail(TEXT("continuation is already linked"));
	}
	const EFactoryConnectionDirection OutsideDirection = End.Outside->GetDirection();
	if (OutsideDirection != EFactoryConnectionDirection::FCD_ANY && OutsideDirection != LiftAutoConnect::Opposite(End.LiftDirection))
	{
		return Fail(TEXT("continuation direction disagrees with the flow"));
	}
	return true;
}

bool FSeamLinker::LinkBridge(AFGBuildableConveyorLift* Lift, FBridgePlan& Plan)
{
	if (!IsValid(Lift))
	{
		return Reject(TEXT("Bridge"), TEXT("lift was not constructed"), FString());
	}
	const FString Context = GetNameSafe(Lift);
	FSeamTransaction Transaction;
	auto Abort = [&](const TCHAR* Reason)
	{
		DetachLift(Lift, Plan, Transaction);
		return Reject(TEXT("Bridge"), Reason, Context);
	};

	// Attachment-first placement can swap the lift's connection slots, so tell input from output by direction.
	UFGFactoryConnectionComponent* Connection0 = Lift->GetConnection0();
	UFGFactoryConnectionComponent* Connection1 = Lift->GetConnection1();
	if (!IsValid(Connection0) || !IsValid(Connection1))
	{
		return Abort(TEXT("lift has no connections"));
	}
	const bool bZeroIsInput = Connection0->GetDirection() == EFactoryConnectionDirection::FCD_INPUT &&
		Connection1->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT;
	const bool bOneIsInput = Connection1->GetDirection() == EFactoryConnectionDirection::FCD_INPUT &&
		Connection0->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT;
	if (!bZeroIsInput && !bOneIsInput)
	{
		return Abort(TEXT("lift connection directions"));
	}
	Plan.Input.LiftConnection = bZeroIsInput ? Connection0 : Connection1;
	Plan.Output.LiftConnection = bZeroIsInput ? Connection1 : Connection0;
	if (Lift->IsFlowUpwards() != (Plan.Flow == ELiftFlow::Up))
	{
		return Abort(TEXT("lift flow disagrees with the preview"));
	}
	if (!PrepareLiftEnd(TEXT("input"), Plan.Input) || !PrepareLiftEnd(TEXT("output"), Plan.Output))
	{
		return Abort(TEXT("an end changed during construction"));
	}

	// Floor hole sides and directions of both ends are set before either end links.
	for (FBridgeEnd* End : {&Plan.Input, &Plan.Output})
	{
		if (End->Outside)
		{
			const EFactoryConnectionDirection Required = LiftAutoConnect::Opposite(End->LiftDirection);
			if (End->Outside->GetDirection() == EFactoryConnectionDirection::FCD_ANY)
			{
				Transaction.SetDirection(End->Outside, Required);
			}
			if (End->Outside->GetDirection() != Required)
			{
				return Abort(TEXT("continuation direction"));
			}
		}
		if (AFGBuildablePassthrough* Hole = End->Endpoint.GetFloorHole())
		{
			UFGFactoryConnectionComponent* Snapped = LiftAutoConnect::GetFloorHoleSnapped(Hole, End->Endpoint.Facing);
			if (Snapped == nullptr)
			{
				Transaction.SetFloorHoleSnapped(Hole, End->Endpoint.Facing, End->LiftConnection);
			}
			else if (Snapped != End->LiftConnection)
			{
				return Abort(TEXT("floor hole side is taken"));
			}
		}
		if (End->Outside && !End->bAlreadyLinked &&
			!End->LiftConnection->CanConnectTo(End->Outside) && !End->Outside->CanConnectTo(End->LiftConnection))
		{
			return Abort(TEXT("link refused"));
		}
	}
	for (FBridgeEnd* End : {&Plan.Input, &Plan.Output})
	{
		if (End->Outside && !End->bAlreadyLinked && !Transaction.Link(End->LiftConnection, End->Outside))
		{
			return Abort(TEXT("link refused"));
		}
	}

	Lift->OnRep_SnappedPassthroughs();
	if (!IsBridgeEndLinked(TEXT("input"), Plan.Input) || !IsBridgeEndLinked(TEXT("output"), Plan.Output))
	{
		return Abort(TEXT("links did not hold"));
	}

	Regroup(Lift);
	UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Linked bridge %s flow=%s input=%s output=%s"),
		*Context, LiftAutoConnect::ToString(Plan.Flow), *Plan.Input.Endpoint.Describe(), *Plan.Output.Endpoint.Describe());

	TArray<FSeamAuditLink> AuditLinks;
	TArray<TWeakObjectPtr<AFGBuildableConveyorBase>> AuditConveyors = {Lift};
	for (const FBridgeEnd* End : {&Plan.Input, &Plan.Output})
	{
		FSeamAuditLink& Link = AuditLinks.AddDefaulted_GetRef();
		Link.Connection = End->LiftConnection;
		Link.Peer = End->Outside;
		Link.Direction = End->LiftDirection;
		Link.FloorHole = End->Endpoint.GetFloorHole();
		Link.FloorHoleSide = End->Endpoint.Facing;
		Link.SideSnapped = End->LiftConnection;
		Link.OppositeSnapped = End->Outside;
		if (AFGBuildableConveyorBase* Neighbor = GetConveyor(End->Outside))
		{
			AuditConveyors.Add(Neighbor);
		}
	}
	FSeamAudit::Schedule(Lift->GetWorld(), Context, MoveTemp(AuditLinks), MoveTemp(AuditConveyors));
	return true;
}

bool FSeamLinker::PrepareLiftEnd(const TCHAR* Name, FBridgeEnd& End)
{
	const FString Context = FString::Printf(TEXT("%s end %s"), Name, *End.Endpoint.Describe());
	auto Fail = [&Context](const TCHAR* Reason) { return Reject(TEXT("Bridge"), Reason, Context); };
	UFGFactoryConnectionComponent* LiftConnection = End.LiftConnection;

	End.bAlreadyLinked = false;
	if (LiftConnection->GetDirection() != End.LiftDirection)
	{
		return Fail(TEXT("lift connection direction"));
	}
	if (End.Endpoint.GetContinuation() != End.Outside)
	{
		return Fail(TEXT("continuation changed"));
	}
	if (AFGBuildablePassthrough* Hole = End.Endpoint.GetFloorHole())
	{
		UFGFactoryConnectionComponent* Snapped = LiftAutoConnect::GetFloorHoleSnapped(Hole, End.Endpoint.Facing);
		if (Snapped != nullptr && Snapped != LiftConnection)
		{
			return Fail(TEXT("floor hole side changed"));
		}
	}

	if (End.Outside == nullptr)
	{
		return IsUnlinked(LiftConnection) || Fail(TEXT("bare end is linked"));
	}
	if (LiftConnection->GetConnection() == End.Outside && End.Outside->GetConnection() == LiftConnection)
	{
		// Vanilla construction linked this end already.
		if (!IsLinkedPair(LiftConnection, End.Outside))
		{
			return Fail(TEXT("link flags disagree"));
		}
		End.bAlreadyLinked = true;
	}
	else if (!IsUnlinked(LiftConnection) || !IsUnlinked(End.Outside))
	{
		return Fail(TEXT("connection is linked elsewhere"));
	}

	const EFactoryConnectionDirection OutsideDirection = End.Outside->GetDirection();
	if (OutsideDirection != EFactoryConnectionDirection::FCD_ANY && OutsideDirection != LiftAutoConnect::Opposite(End.LiftDirection))
	{
		return Fail(TEXT("continuation direction changed"));
	}
	return true;
}

bool FSeamLinker::IsBridgeEndLinked(const TCHAR* Name, const FBridgeEnd& End)
{
	UFGFactoryConnectionComponent* LiftConnection = End.LiftConnection;
	bool bHolds = IsValid(LiftConnection) && LiftConnection->GetDirection() == End.LiftDirection &&
		End.Endpoint.GetContinuation() == End.Outside;
	if (bHolds && End.Endpoint.Kind == ELiftEndpointKind::FloorHole)
	{
		bHolds = LiftAutoConnect::GetFloorHoleSnapped(End.Endpoint.GetFloorHole(), End.Endpoint.Facing) == LiftConnection;
	}
	if (bHolds)
	{
		bHolds = End.Outside
			? End.Outside->GetDirection() == LiftAutoConnect::Opposite(End.LiftDirection) && IsLinkedPair(LiftConnection, End.Outside)
			: IsUnlinked(LiftConnection);
	}
	if (!bHolds)
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Bridge %s end %s: lift connection %s -> %s, continuation %s -> %s"),
			Name, *End.Endpoint.Describe(),
			*GetNameSafe(LiftConnection), IsValid(LiftConnection) ? *GetNameSafe(LiftConnection->GetConnection()) : TEXT("?"),
			*GetNameSafe(End.Outside), IsValid(End.Outside) ? *GetNameSafe(End.Outside->GetConnection()) : TEXT("-"));
	}
	return bHolds;
}

void FSeamLinker::DetachLift(AFGBuildableConveyorLift* Lift, const FBridgePlan& Plan, FSeamTransaction& Transaction)
{
	// Vanilla construction can link the lift itself, so clear every link of the lift, then undo the recorded changes.
	for (UFGFactoryConnectionComponent* Connection : {Lift->GetConnection0(), Lift->GetConnection1()})
	{
		if (!IsValid(Connection))
		{
			continue;
		}
		UFGFactoryConnectionComponent* Peer = Connection->GetConnection();
		if (IsValid(Peer) && Peer->GetConnection() == Connection)
		{
			Connection->ClearConnection();
		}
		else if (Peer != nullptr || Connection->IsConnected())
		{
			UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Lift %s connection %s points at %s, which does not point back"),
				*GetNameSafe(Lift), *GetNameSafe(Connection), *GetNameSafe(Peer));
		}
	}
	Transaction.Rollback();
	for (const FBridgeEnd* End : {&Plan.Input, &Plan.Output})
	{
		AFGBuildablePassthrough* Hole = End->Endpoint.GetFloorHole();
		UFGFactoryConnectionComponent* Snapped = LiftAutoConnect::GetFloorHoleSnapped(Hole, End->Endpoint.Facing);
		if (IsValid(Snapped) && Snapped->GetOwner() == Lift)
		{
			LiftAutoConnect::SetFloorHoleSnapped(Hole, End->Endpoint.Facing, nullptr);
		}
	}
	Lift->OnRep_SnappedPassthroughs();
}

bool FSeamLinker::IsLiftConnection(UFGFactoryConnectionComponent* Connection)
{
	AFGBuildableConveyorLift* Lift = IsValid(Connection) ? Cast<AFGBuildableConveyorLift>(Connection->GetOwner()) : nullptr;
	return IsValid(Lift) && (Connection == Lift->GetConnection0() || Connection == Lift->GetConnection1());
}

void FSeamLinker::Regroup(AFGBuildableConveyorBase* Conveyor)
{
	// AddConveyor merges buckets only with neighbors linked at that moment. A conveyor registered before it was linked
	// (a generated lift registers in Construct) keeps its own bucket, and the save then holds two chain actors on one
	// path. This repeats what AFGBuildableSubsystem::SplitConveyorGroupFromAttachment does.
	if (!IsValid(Conveyor) || Conveyor->GetConveyorBucketID() == INDEX_NONE)
	{
		return;
	}
	AFGBuildableSubsystem* Subsystem = AFGBuildableSubsystem::Get(Conveyor);
	if (!IsValid(Subsystem) || !Subsystem->IsServerSubSystem())
	{
		return;
	}

	auto GetRegisteredNeighbor = [](UFGFactoryConnectionComponent* Connection) -> AFGBuildableConveyorBase*
	{
		AFGBuildableConveyorBase* Neighbor = GetConveyor(IsValid(Connection) ? Connection->GetConnection() : nullptr);
		return IsValid(Neighbor) && Neighbor->GetConveyorBucketID() != INDEX_NONE ? Neighbor : nullptr;
	};
	AFGBuildableConveyorBase* Neighbor0 = GetRegisteredNeighbor(Conveyor->GetConnection0());
	AFGBuildableConveyorBase* Neighbor1 = GetRegisteredNeighbor(Conveyor->GetConnection1());
	auto IsSplit = [Conveyor](const AFGBuildableConveyorBase* Neighbor)
	{
		return Neighbor != nullptr && Neighbor->GetConveyorBucketID() != Conveyor->GetConveyorBucketID();
	};
	if (!IsSplit(Neighbor0) && !IsSplit(Neighbor1))
	{
		return;
	}

	const int32 BucketBefore = Conveyor->GetConveyorBucketID();
	Subsystem->RemoveConveyor(Conveyor);
	Subsystem->AddConveyor(Conveyor);
	Subsystem->mConveyorBucketGroupsDirty = true;

	const FString Detail = FString::Printf(TEXT("%s bucket %d -> %d (neighbors %s:%d, %s:%d)"),
		*GetNameSafe(Conveyor), BucketBefore, Conveyor->GetConveyorBucketID(),
		*GetNameSafe(Neighbor0), Neighbor0 ? Neighbor0->GetConveyorBucketID() : INDEX_NONE,
		*GetNameSafe(Neighbor1), Neighbor1 ? Neighbor1->GetConveyorBucketID() : INDEX_NONE);
	if (Conveyor->GetConveyorBucketID() == INDEX_NONE || IsSplit(Neighbor0) || IsSplit(Neighbor1))
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Regrouping left split buckets: %s"), *Detail);
	}
	else
	{
		UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Regrouped %s"), *Detail);
	}
}
