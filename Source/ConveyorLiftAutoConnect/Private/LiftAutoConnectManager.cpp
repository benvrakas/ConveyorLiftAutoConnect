#include "LiftAutoConnectManager.h"

#include "Buildables/FGBuildableConveyorLift.h"
#include "ConveyorLiftAutoConnectModule.h"
#include "FGBuildableSubsystem.h"
#include "FGConstructDisqualifier.h"
#include "FGRecipe.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "LiftBridgeHologram.h"
#include "Misc/ScopeExit.h"

bool FLiftAutoConnectManager::FBridgeConfigKey::operator==(const FBridgeConfigKey& Other) const
{
	return Bridge == Other.Bridge && Recipe == Other.Recipe && Target == Other.Target && Flow == Other.Flow &&
		SourceTransform.Equals(Other.SourceTransform, 0.0) && TargetTransform.Equals(Other.TargetTransform, 0.0);
}

FLiftEndpoint FLiftAutoConnectManager::FSeamState::GetTarget() const
{
	FLiftEndpoint Target;
	Target.Buildable = TargetBuildable.Get();
	Target.Kind = TargetKind;
	Target.Facing = TargetFacing;
	Target.Port = TargetPort;
	return Target;
}

void FLiftAutoConnectManager::FSeamState::SetTarget(const FLiftEndpoint& Target)
{
	TargetBuildable = Target.Buildable;
	TargetKind = Target.Kind;
	TargetFacing = Target.Facing;
	TargetPort = Target.Port;
}

void FLiftAutoConnectManager::FSeamState::ClearTarget()
{
	SetTarget(FLiftEndpoint());
	Flow = ELiftFlow::Unknown;
	bDirect = false;
	DirectSourceDirection = EFactoryConnectionDirection::FCD_ANY;
	DirectTargetDirection = EFactoryConnectionDirection::FCD_ANY;
	bValid = false;
}

FBlueprintContinuation FLiftAutoConnectManager::FSeamState::GetBlueprintContinuation() const
{
	return {bHadContinuation, ConstructedContinuation};
}

FLiftAutoConnectManager::FLiftAutoConnectManager(AFGBlueprintHologram* BlueprintHologram)
	: FGBlueprintOpenConnectionManagerBase(BlueprintHologram)
{
}

void FLiftAutoConnectManager::RegisterNearbyActor(AActor* Actor)
{
	AFGBuildable* Buildable = Cast<AFGBuildable>(Actor);
	if (FLiftEndpoint::IsSupportedBuildable(Buildable) && !NearbyBuildables.Contains(Buildable))
	{
		NearbyBuildables.Add(Buildable);
	}
}

void FLiftAutoConnectManager::UnregisterNearbyActor(AActor* Actor)
{
	NearbyBuildables.RemoveAllSwap([Actor](const TWeakObjectPtr<AFGBuildable>& Buildable) { return Buildable.Get() == Actor; });
}

void FLiftAutoConnectManager::Initialize(TArray<AFGBuildable*> Buildables)
{
	const double StartSeconds = FPlatformTime::Seconds();
	Settings = ULiftAutoConnectConfig::Read(GetHologram());
	ULiftAutoConnectConfig::ApplyLogging(Settings);

	for (FSeamState& State : States)
	{
		DisableBridge(State);
	}
	States.Reset();
	BlueprintEndpoints.Reset();
	ReportedBlockedStates.Reset();

	AFGBlueprintHologram* Hologram = GetHologram();
	TArray<FLiftEndpoint> OpenEndpoints;
	for (int32 Index = 0; Index < Buildables.Num(); ++Index)
	{
		FLiftEndpoint::Gather(Buildables[Index], Hologram, false, BlueprintEndpoints);
		OpenEndpoints.Reset();
		FLiftEndpoint::Gather(Buildables[Index], Hologram, true, OpenEndpoints);

		for (const FLiftEndpoint& Endpoint : OpenEndpoints)
		{
			FSeamState& State = States.AddDefaulted_GetRef();
			State.Source = Endpoint;
			State.SourceIndex = Index;
			if (Endpoint.Kind != ELiftEndpointKind::FloorHole)
			{
				continue;
			}

			UFGFactoryConnectionComponent* Continuation = Endpoint.GetContinuation();
			State.bHadContinuation = Continuation != nullptr;
			if (!IsValid(Continuation))
			{
				continue;
			}
			AFGBuildableConveyorLift* Lift = Cast<AFGBuildableConveyorLift>(Continuation->GetOwner());
			State.ContinuationIndex = IsValid(Lift) ? Buildables.IndexOfByKey(Lift) : INDEX_NONE;
			State.ContinuationSlot = !IsValid(Lift) ? INDEX_NONE
				: Continuation == Lift->GetConnection0() ? 0
				: Continuation == Lift->GetConnection1() ? 1
				: INDEX_NONE;
			if (State.ContinuationIndex == INDEX_NONE || State.ContinuationSlot == INDEX_NONE)
			{
				UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Cannot locate the lift behind %s in the blueprint (%s)"),
					*Endpoint.Describe(), *GetNameSafe(Continuation));
			}
		}
	}

	UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Initialized %d open vertical endpoints in %.1f ms"),
		States.Num(), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void FLiftAutoConnectManager::UpdateAutomaticConnections(const FHitResult& /*HitResult*/, bool& OutPlaySnapEffects)
{
	const double StartSeconds = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		const double Elapsed = FPlatformTime::Seconds() - StartSeconds;
		UpdateTimingTotalSeconds += Elapsed;
		UpdateTimingMaxSeconds = FMath::Max(UpdateTimingMaxSeconds, Elapsed);
		if (++UpdateTimingCount >= UpdateTimingInterval)
		{
			UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Preview update timing: %d states, %d calls, avg %.2f ms, max %.2f ms"),
				States.Num(), UpdateTimingCount, UpdateTimingTotalSeconds * 1000.0 / UpdateTimingCount, UpdateTimingMaxSeconds * 1000.0);
			UpdateTimingCount = 0;
			UpdateTimingTotalSeconds = 0.0;
			UpdateTimingMaxSeconds = 0.0;
		}
	};

	NearbyBuildables.RemoveAllSwap([](const TWeakObjectPtr<AFGBuildable>& Buildable) { return !Buildable.IsValid(); });

	Settings = ULiftAutoConnectConfig::Read(GetHologram());
	if (!Settings.bEnabled)
	{
		ResetAutomaticConnections();
		return;
	}

	TArray<FLocatedEndpoint> Targets;
	GatherTargets(Targets);
	TArray<FLocatedEndpoint> Blockers;
	Blockers.Reserve(BlueprintEndpoints.Num());
	for (const FLiftEndpoint& Endpoint : BlueprintEndpoints)
	{
		Blockers.Add({Endpoint, Endpoint.GetLocation()});
	}

	TSet<FLiftEndpoint> Claimed;
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		FSeamState& State = States[Index];
		const FLiftEndpoint PreviousTarget = State.GetTarget();
		const bool bPreviouslyValid = State.bValid;

		UpdateState(State, Index, Claimed, Targets, Blockers);

		const FLiftEndpoint Target = State.GetTarget();
		if (State.bValid && Target.HasBuildable())
		{
			Claimed.Add(Target);
		}
		if (Target == PreviousTarget && State.bValid == bPreviouslyValid)
		{
			continue;
		}

		BroadcastStateChange(State, PreviousTarget);
		OutPlaySnapEffects |= State.bValid;
		UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("State %d: %s -> %s valid=%d direct=%d flow=%s recipe=%s"),
			Index, *State.Source.Describe(), *Target.Describe(), State.bValid ? 1 : 0, State.bDirect ? 1 : 0,
			LiftAutoConnect::ToString(State.Flow), *GetNameSafe(State.Recipe.Get()));
	}
}

void FLiftAutoConnectManager::GatherTargets(TArray<FLocatedEndpoint>& OutTargets) const
{
	TSet<AFGBuildable*> Seen;
	TArray<AFGBuildable*> Buildables;
	auto AddBuildable = [&Seen, &Buildables](AFGBuildable* Buildable)
	{
		if (FLiftEndpoint::IsSupportedBuildable(Buildable) && !Seen.Contains(Buildable))
		{
			Seen.Add(Buildable);
			Buildables.Add(Buildable);
		}
	};
	for (const TWeakObjectPtr<AFGBuildable>& Buildable : NearbyBuildables)
	{
		AddBuildable(Buildable.Get());
	}

	// The hologram's overlap feed only reaches near the blueprint bounds, so also query every buildable in the
	// full-height prism over the source endpoints. Lift length is left to the vanilla lift hologram.
	FBox SourceBounds(ForceInit);
	for (const FSeamState& State : States)
	{
		if (State.Source.HasBuildable())
		{
			SourceBounds += State.Source.GetLocation();
		}
	}
	AFGBuildableSubsystem* Subsystem = AFGBuildableSubsystem::Get(GetHologram());
	if (SourceBounds.IsValid && IsValid(Subsystem))
	{
		SourceBounds.Min.Z = -WORLD_MAX;
		SourceBounds.Max.Z = WORLD_MAX;
		TArray<AFGBuildable*> InPrism;
		Subsystem->GetCollidingBuildablesInBoundingBox(InPrism, SourceBounds);
		for (AFGBuildable* Buildable : InPrism)
		{
			AddBuildable(Buildable);
		}
	}

	TArray<FLiftEndpoint> Endpoints;
	for (AFGBuildable* Buildable : Buildables)
	{
		FLiftEndpoint::Gather(Buildable, nullptr, false, Endpoints);
	}
	OutTargets.Reserve(Endpoints.Num());
	for (const FLiftEndpoint& Endpoint : Endpoints)
	{
		OutTargets.Add({Endpoint, Endpoint.GetLocation()});
	}
}

void FLiftAutoConnectManager::UpdateState(
	FSeamState& State,
	int32 StateIndex,
	const TSet<FLiftEndpoint>& Claimed,
	const TArray<FLocatedEndpoint>& Targets,
	const TArray<FLocatedEndpoint>& Blockers)
{
	// A snapped seam stays as it is until its target disappears.
	if (State.bSnapped && State.TargetBuildable.IsValid())
	{
		return;
	}
	State.bSnapped = false;
	State.ClearTarget();
	if (!State.Source.IsOpen())
	{
		DisableBridge(State);
		return;
	}

	const FVector SourceLocation = State.Source.GetLocation();
	TArray<FSeamCandidate> Candidates;
	FLiftEndpoint Blocker;
	SeamSearch::FindNearestPlane(State.Source, SourceLocation, Targets, Blockers, Candidates, &Blocker);
	if (Blocker.HasBuildable() && !ReportedBlockedStates.Contains(StateIndex))
	{
		ReportedBlockedStates.Add(StateIndex);
		UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("State %d: %s is blocked by blueprint endpoint %s"),
			StateIndex, *State.Source.Describe(), *Blocker.Describe());
	}

	const bool bExplain = UE_LOG_ACTIVE(LogConveyorLiftAutoConnect, Verbose);
	FString Refusals;
	auto Refuse = [bExplain, &Refusals](const FLiftEndpoint& Target, const FString& Reason)
	{
		if (bExplain)
		{
			Refusals += FString::Printf(TEXT(" %s: %s;"), *Target.Describe(), *Reason);
		}
	};

	// Only the nearest plane is tried, so a lift never passes through a closer endpoint.
	for (const FSeamCandidate& Candidate : Candidates)
	{
		const FLiftEndpoint& Target = Candidate.Target;
		if (!Target.IsOpen() || Claimed.Contains(Target))
		{
			Refuse(Target, Target.IsOpen() ? TEXT("claimed by another endpoint") : TEXT("taken"));
			continue;
		}
		const TSubclassOf<UFGRecipe> Recipe = ResolveRecipe(State.Source, Target);
		if (!Recipe)
		{
			Refuse(Target, TEXT("no adjacent lift on either side"));
			continue;
		}

		if (Candidate.Geometry == ESeamGeometry::Direct)
		{
			EFactoryConnectionDirection SourceDirection;
			EFactoryConnectionDirection TargetDirection;
			if (!SeamSearch::CanLinkDirectly(State.Source.GetContinuation(), Target.GetContinuation(), SourceDirection, TargetDirection))
			{
				Refuse(Target, TEXT("touching, but the conveyors cannot be linked"));
				continue;
			}
			State.SetTarget(Target);
			State.bDirect = true;
			State.DirectSourceDirection = SourceDirection;
			State.DirectTargetDirection = TargetDirection;
			State.Recipe = Recipe;
			State.bValid = true;
			State.LastRefusal.Reset();
			DisableBridge(State);
			return;
		}

		if (Settings.MaxLiftHeight > 0.0f && Candidate.Distance > Settings.MaxLiftHeight)
		{
			Refuse(Target, FString::Printf(TEXT("lift of %.1f m is longer than the setting allows"), Candidate.Distance / 100.0f));
			continue;
		}
		const ELiftFlow Flow = SeamSearch::ResolveFlow(State.Source, Target);
		if (Flow == ELiftFlow::Unknown)
		{
			Refuse(Target, FString::Printf(TEXT("flow unresolved (source %s, target %s)"),
				LiftAutoConnect::ToString(State.Source.GetImpliedFlow()), LiftAutoConnect::ToString(Target.GetImpliedFlow())));
			continue;
		}
		State.SetTarget(Target);
		State.Flow = Flow;
		if (EnsureBridge(State, StateIndex, Recipe) && ConfigurePreviewBridge(State))
		{
			State.bValid = true;
			State.LastRefusal.Reset();
			return;
		}
		if (bExplain)
		{
			TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
			if (IsValid(State.Bridge))
			{
				State.Bridge->GetConstructDisqualifiers(Disqualifiers);
			}
			FString Names;
			for (const TSubclassOf<UFGConstructDisqualifier>& Disqualifier : Disqualifiers)
			{
				Names += (Names.IsEmpty() ? TEXT("") : TEXT(",")) + GetNameSafe(Disqualifier.Get());
			}
			Refuse(Target, FString::Printf(TEXT("lift %s refused [%s]"), LiftAutoConnect::ToString(Flow), *Names));
		}
	}

	State.ClearTarget();
	DisableBridge(State);

	if (bExplain)
	{
		if (Candidates.IsEmpty())
		{
			// The closest world endpoint by horizontal distance, to show near misses of the column match.
			const FLocatedEndpoint* Closest = nullptr;
			for (const FLocatedEndpoint& Target : Targets)
			{
				if (!Closest || FVector::DistSquared2D(Target.Location, SourceLocation) < FVector::DistSquared2D(Closest->Location, SourceLocation))
				{
					Closest = &Target;
				}
			}
			Refusals = Closest
				? FString::Printf(TEXT(" no endpoint in the column (%d world endpoints; closest %s off by x=%.4f y=%.4f z=%.1f)"),
					Targets.Num(), *Closest->Endpoint.Describe(), Closest->Location.X - SourceLocation.X,
					Closest->Location.Y - SourceLocation.Y, Closest->Location.Z - SourceLocation.Z)
				: TEXT(" no world endpoints found");
		}
		// Log when the kind of refusal changes, not on every moved frame.
		FString Signature = Refusals;
		for (TCHAR& Char : Signature)
		{
			if (FChar::IsDigit(Char))
			{
				Char = TEXT('#');
			}
		}
		if (Signature != State.LastRefusal)
		{
			State.LastRefusal = Signature;
			UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("State %d: %s found no seam:%s"), StateIndex, *State.Source.Describe(), *Refusals);
		}
	}
}

TSubclassOf<UFGRecipe> FLiftAutoConnectManager::ResolveRecipe(const FLiftEndpoint& Source, const FLiftEndpoint& Target) const
{
	AFGBuildableConveyorLift* SourceLift = Source.GetAdjacentLift();
	AFGBuildableConveyorLift* TargetLift = Target.GetAdjacentLift();
	AFGBuildableConveyorLift* Chosen = nullptr;
	switch (Settings.TierSource)
	{
	case ELiftTierSource::World:
		Chosen = TargetLift ? TargetLift : SourceLift;
		break;
	case ELiftTierSource::Faster:
		Chosen = SourceLift && TargetLift
			? (TargetLift->GetSpeed() > SourceLift->GetSpeed() ? TargetLift : SourceLift)
			: (SourceLift ? SourceLift : TargetLift);
		break;
	default:
		Chosen = SourceLift ? SourceLift : TargetLift;
		break;
	}
	return Chosen ? Chosen->GetBuiltWithRecipe() : nullptr;
}

bool FLiftAutoConnectManager::EnsureBridge(FSeamState& State, int32 StateIndex, TSubclassOf<UFGRecipe> Recipe)
{
	if (State.Recipe != Recipe || !IsValid(State.Bridge))
	{
		DisableBridge(State);
		State.Recipe = Recipe;
		State.Bridge = FLiftBridgeHologram::FindOrSpawn(GetHologram(), StateIndex, Recipe);
		State.ConfiguredBridge.Reset();
	}
	return IsValid(State.Bridge);
}

bool FLiftAutoConnectManager::ConfigurePreviewBridge(FSeamState& State)
{
	// Configuring rebuilds the lift mesh and reruns vanilla validation; skip it while nothing has moved.
	const FLiftEndpoint Target = State.GetTarget();
	const FBridgeConfigKey Key{State.Bridge, State.Recipe, Target, State.Source.GetWorldTransform(), Target.GetWorldTransform(), State.Flow};
	if (State.ConfiguredBridge.IsSet() && State.ConfiguredBridge.GetValue() == Key && !State.Bridge->IsDisabled())
	{
		return true;
	}

	const bool bCanConstruct = FLiftBridgeHologram::Configure(State.Bridge, State.Source, Target, State.Flow, false);
	if (bCanConstruct)
	{
		State.ConfiguredBridge = Key;
	}
	else
	{
		State.ConfiguredBridge.Reset();
	}
	return bCanConstruct;
}

void FLiftAutoConnectManager::DisableBridge(FSeamState& State)
{
	if (IsValid(State.Bridge))
	{
		State.Bridge->SetDisabled(true);
	}
}

void FLiftAutoConnectManager::BroadcastStateChange(const FSeamState& State, const FLiftEndpoint& PreviousTarget)
{
	// A bare floor hole has no connection indicator to swap.
	UFGFactoryConnectionComponent* Representation = State.Source.GetRepresentationConnection();
	if (!IsValid(Representation))
	{
		return;
	}
	const TArray<UFGConnectionComponent*> Connections = {Representation};
	mOnConnectionStateChanged.Broadcast(Connections, PreviousTarget.GetContinuation(), State.GetTarget().GetContinuation(), State.bValid);
}

bool FLiftAutoConnectManager::AttemptConnectionStateSnap()
{
	bool bSnappedAny = false;
	for (FSeamState& State : States)
	{
		if (State.bValid && State.TargetBuildable.IsValid() && !State.bSnapped)
		{
			State.bSnapped = true;
			bSnappedAny = true;
			UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Locked %s -> %s"), *State.Source.Describe(), *State.GetTarget().Describe());
		}
	}
	return bSnappedAny;
}

bool FLiftAutoConnectManager::CanSnapConnectionStates() const
{
	return States.ContainsByPredicate([](const FSeamState& State)
	{
		return State.bValid && State.TargetBuildable.IsValid() && !State.bSnapped;
	});
}

void FLiftAutoConnectManager::ResetAutomaticConnections()
{
	for (FSeamState& State : States)
	{
		const FLiftEndpoint PreviousTarget = State.GetTarget();
		const bool bShouldBroadcast = State.bValid || PreviousTarget.HasBuildable();
		State.ClearTarget();
		State.ConstructedSource = nullptr;
		State.ConstructedContinuation = nullptr;
		State.bSnapped = false;
		DisableBridge(State);
		if (bShouldBroadcast)
		{
			BroadcastStateChange(State, PreviousTarget);
		}
	}
}

void FLiftAutoConnectManager::HandleBuildableConnectionRemapping(AFGBuildable* Buildable, int32 BlueprintBuildableIndex)
{
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		FSeamState& State = States[Index];
		if (State.ContinuationIndex == BlueprintBuildableIndex && State.ContinuationSlot != INDEX_NONE)
		{
			AFGBuildableConveyorLift* Lift = Cast<AFGBuildableConveyorLift>(Buildable);
			UFGFactoryConnectionComponent* Continuation = !IsValid(Lift) ? nullptr
				: State.ContinuationSlot == 0 ? Lift->GetConnection0()
				: Lift->GetConnection1();
			State.ConstructedContinuation = IsValid(Continuation) ? Continuation : nullptr;
			if (State.ConstructedContinuation)
			{
				UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("State %d: lift behind the source is %s"), Index, *GetNameSafe(Continuation));
			}
			else
			{
				UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("State %d: constructed %s has no connection %d for the lift behind the source"),
					Index, *GetNameSafe(Buildable), State.ContinuationSlot);
			}
		}
		if (State.SourceIndex == BlueprintBuildableIndex && FLiftEndpoint::IsSupportedBuildable(Buildable))
		{
			State.ConstructedSource = Buildable;
			UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("State %d: %s was constructed as %s"),
				Index, *State.Source.Describe(), *GetNameSafe(Buildable));
		}
	}
}

void FLiftAutoConnectManager::SerializeConstructMessage(FArchive& Archive, FNetConstructionID /*Id*/)
{
	// The blueprint's own data nearly fills the 64 KB message, so only one bit per seam and the placing player's
	// search settings are sent; the receiver searches the same world again in PostConstructMessageDeserialization.
	uint8 TierSource = static_cast<uint8>(Settings.TierSource);
	float MaxLiftHeight = Settings.MaxLiftHeight;
	int32 StateCount = States.Num();
	Archive << TierSource << MaxLiftHeight << StateCount;
	if (Archive.IsLoading())
	{
		Settings.TierSource = static_cast<ELiftTierSource>(TierSource);
		Settings.MaxLiftHeight = MaxLiftHeight;
	}
	if (StateCount < 0 || StateCount > MaxSerializedStates)
	{
		UE_LOG(LogConveyorLiftAutoConnect, Error, TEXT("Construct message has an invalid seam count %d; connecting none"), StateCount);
		for (FSeamState& State : States)
		{
			State.bConnectOnConstruct = false;
		}
		Archive.SetError();
		return;
	}
	const bool bMatches = StateCount == States.Num();
	if (!bMatches)
	{
		UE_LOG(LogConveyorLiftAutoConnect, Error, TEXT("Construct message has %d seams, the blueprint has %d; connecting none"),
			StateCount, States.Num());
	}

	// The bytes are always read in full so the data after them stays aligned.
	for (int32 First = 0; First < StateCount; First += 8)
	{
		uint8 Bits = 0;
		for (int32 Bit = 0; Bit < 8 && First + Bit < StateCount && bMatches; ++Bit)
		{
			const FSeamState& State = States[First + Bit];
			Bits |= State.bValid && State.TargetBuildable.IsValid() ? 1 << Bit : 0;
		}
		Archive << Bits;
		for (int32 Bit = 0; Bit < 8 && First + Bit < StateCount && bMatches; ++Bit)
		{
			States[First + Bit].bConnectOnConstruct = (Bits & 1 << Bit) != 0;
		}
	}
	if (!bMatches)
	{
		for (FSeamState& State : States)
		{
			State.bConnectOnConstruct = false;
		}
	}
}

void FLiftAutoConnectManager::PostConstructMessageDeserialization()
{
	// Rebuild the seams the placing player saw, in the same order so claims resolve the same way.
	TArray<FLocatedEndpoint> Targets;
	GatherTargets(Targets);
	TArray<FLocatedEndpoint> Blockers;
	Blockers.Reserve(BlueprintEndpoints.Num());
	for (const FLiftEndpoint& Endpoint : BlueprintEndpoints)
	{
		Blockers.Add({Endpoint, Endpoint.GetLocation()});
	}

	TSet<FLiftEndpoint> Claimed;
	int32 Requested = 0;
	int32 Rebuilt = 0;
	for (int32 Index = 0; Index < States.Num(); ++Index)
	{
		FSeamState& State = States[Index];
		State.bSnapped = false;
		State.ClearTarget();
		if (!State.bConnectOnConstruct)
		{
			DisableBridge(State);
			continue;
		}
		++Requested;
		UpdateState(State, Index, Claimed, Targets, Blockers);
		if (State.bValid && State.TargetBuildable.IsValid())
		{
			Claimed.Add(State.GetTarget());
			State.bSnapped = true;
			++Rebuilt;
		}
		else
		{
			UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("State %d: %s connected in the preview but found no seam on construction"),
				Index, *State.Source.Describe());
		}
	}
	UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Construct message: rebuilt %d of %d requested seams (%d world endpoints)"),
		Rebuilt, Requested, Targets.Num());
}

void FLiftAutoConnectManager::Construct(TArray<AFGBuildable*>& OutConstructedBridgeBuildables, FNetConstructionID NetConstructionID)
{
	const double StartSeconds = FPlatformTime::Seconds();
	const int32 CountBefore = OutConstructedBridgeBuildables.Num();
	for (FSeamState& State : States)
	{
		if (State.bValid && State.TargetBuildable.IsValid() && IsValid(State.ConstructedSource))
		{
			ConstructSeam(State, OutConstructedBridgeBuildables, NetConstructionID);
		}
	}
	UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Constructed %d bridge buildables for %d states in %.1f ms"),
		OutConstructedBridgeBuildables.Num() - CountBefore, States.Num(), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void FLiftAutoConnectManager::ConstructSeam(FSeamState& State, TArray<AFGBuildable*>& OutConstructed, FNetConstructionID NetConstructionID)
{
	const FLiftEndpoint Source = State.Source.InWorld(State.ConstructedSource);
	const FLiftEndpoint Target = State.GetTarget();
	const FString Context = FString::Printf(TEXT("%s -> %s"), *Source.Describe(), *Target.Describe());

	if (State.bDirect)
	{
		if (!FSeamLinker::LinkDirect({Source, Target, State.DirectSourceDirection, State.DirectTargetDirection, State.GetBlueprintContinuation()}))
		{
			UE_LOG(LogConveyorLiftAutoConnect, Error, TEXT("Direct seam not linked: %s"), *Context);
		}
		else
		{
			UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Linked directly: %s"), *Context);
		}
		return;
	}

	if (!IsValid(State.Bridge) || !FLiftBridgeHologram::Configure(State.Bridge, Source, Target, State.Flow, true) ||
		State.Bridge->IsDisabled())
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Bridge failed final validation: %s"), *Context);
		return;
	}
	FBridgePlan Plan;
	if (!FSeamLinker::PlanBridge({Source, Target, State.Flow, State.GetBlueprintContinuation()}, Plan))
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Bridge not built: %s"), *Context);
		return;
	}

	TArray<AActor*> Children;
	AFGBuildableConveyorLift* Lift = Cast<AFGBuildableConveyorLift>(State.Bridge->Construct(Children, NetConstructionID));
	if (!IsValid(Lift))
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Bridge construction failed: %s"), *Context);
		return;
	}
	if (!FSeamLinker::LinkBridge(Lift, Plan))
	{
		UE_LOG(LogConveyorLiftAutoConnect, Error, TEXT("Discarding bridge %s, it could not be linked: %s"), *GetNameSafe(Lift), *Context);
		for (AActor* Child : Children)
		{
			if (IsValid(Child))
			{
				Child->Destroy();
			}
		}
		Lift->Destroy();
		return;
	}

	UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Built %s (%s, %.0f cm): %s"), *GetNameSafe(Lift),
		LiftAutoConnect::ToString(State.Flow), FMath::Abs(Target.GetLocation().Z - Source.GetLocation().Z), *Context);
	OutConstructed.Add(Lift);
	for (AActor* Child : Children)
	{
		if (AFGBuildable* ChildBuildable = Cast<AFGBuildable>(Child))
		{
			OutConstructed.Add(ChildBuildable);
		}
	}
}
