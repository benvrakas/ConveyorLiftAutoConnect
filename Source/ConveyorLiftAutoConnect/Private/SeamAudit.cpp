#include "SeamAudit.h"

#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "ConveyorLiftAutoConnectModule.h"
#include "Engine/World.h"
#include "FGBuildableSubsystem.h"
#include "TimerManager.h"

void FSeamAudit::Schedule(
	UWorld* World,
	const FString& Label,
	TArray<FSeamAuditLink> Links,
	TArray<TWeakObjectPtr<AFGBuildableConveyorBase>> Conveyors)
{
	// Scanning tick groups costs time proportional to the factory, so only audit when asked for Verbose logs.
	if (!UE_LOG_ACTIVE(LogConveyorLiftAutoConnect, Verbose) || !IsValid(World))
	{
		return;
	}
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda(
		[Label, Links = MoveTemp(Links), Conveyors = MoveTemp(Conveyors)]()
		{
			Run(Label, Links, Conveyors);
		}));
}

void FSeamAudit::Run(
	const FString& Label,
	const TArray<FSeamAuditLink>& Links,
	const TArray<TWeakObjectPtr<AFGBuildableConveyorBase>>& Conveyors)
{
	bool bPassed = true;
	for (const FSeamAuditLink& Link : Links)
	{
		bPassed &= IsLinkIntact(Link);
	}

	AFGBuildableSubsystem* Subsystem = nullptr;
	int32 SharedBucket = INDEX_NONE;
	FString ConveyorReport;
	for (const TWeakObjectPtr<AFGBuildableConveyorBase>& WeakConveyor : Conveyors)
	{
		AFGBuildableConveyorBase* Conveyor = WeakConveyor.Get();
		if (!IsValid(Conveyor))
		{
			bPassed = false;
			ConveyorReport += TEXT(" <gone>");
			continue;
		}
		if (Subsystem == nullptr)
		{
			Subsystem = AFGBuildableSubsystem::Get(Conveyor);
		}

		// The conveyor must sit in exactly one tick group, whose chain actor is its saved one unless still pending.
		int32 TickGroups = 0;
		bool bPending = false;
		AFGConveyorChainActor* TickChain = nullptr;
		if (IsValid(Subsystem))
		{
			for (FConveyorTickGroup* TickGroup : Subsystem->mConveyorTickGroup)
			{
				if (TickGroup == nullptr || !TickGroup->Conveyors.Contains(Conveyor))
				{
					continue;
				}
				if (++TickGroups == 1)
				{
					TickChain = TickGroup->ChainActor;
				}
				bPending |= Subsystem->mConveyorGroupsPendingChainActors.Contains(TickGroup);
			}
		}
		AFGConveyorChainActor* SavedChain = Conveyor->GetConveyorChainActor();
		const int32 Bucket = Conveyor->GetConveyorBucketID();
		if (SharedBucket == INDEX_NONE)
		{
			SharedBucket = Bucket;
		}
		bPassed &= TickGroups == 1 && (bPending || TickChain == SavedChain) && Bucket == SharedBucket && Bucket != INDEX_NONE;

		ConveyorReport += FString::Printf(TEXT(" %s(bucket=%d chain=%s tickGroups=%d tickChain=%s pending=%d)"),
			*GetNameSafe(Conveyor), Bucket, *GetNameSafe(SavedChain), TickGroups, *GetNameSafe(TickChain), bPending ? 1 : 0);
	}

	if (bPassed)
	{
		UE_LOG(LogConveyorLiftAutoConnect, Verbose, TEXT("Seam audit passed %s:%s"), *Label, *ConveyorReport);
	}
	else
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Seam audit failed %s:%s"), *Label, *ConveyorReport);
	}
}

bool FSeamAudit::IsLinkIntact(const FSeamAuditLink& Link)
{
	UFGFactoryConnectionComponent* Connection = Link.Connection.Get();
	UFGFactoryConnectionComponent* Peer = Link.Peer.Get();
	if (!IsValid(Connection) || Connection->GetDirection() != Link.Direction || Link.Peer.IsStale())
	{
		return false;
	}

	bool bIntact = Peer
		? Connection->GetConnection() == Peer && Peer->GetConnection() == Connection &&
			Connection->IsConnected() && Peer->IsConnected() &&
			Peer->GetDirection() == LiftAutoConnect::Opposite(Link.Direction)
		: Connection->GetConnection() == nullptr && !Connection->IsConnected();

	if (bIntact && !Link.FloorHole.IsExplicitlyNull())
	{
		AFGBuildablePassthrough* Hole = Link.FloorHole.Get();
		bIntact = IsValid(Hole) &&
			LiftAutoConnect::GetFloorHoleSnapped(Hole, Link.FloorHoleSide) == Link.SideSnapped.Get() &&
			LiftAutoConnect::GetFloorHoleSnapped(Hole, LiftAutoConnect::Opposite(Link.FloorHoleSide)) == Link.OppositeSnapped.Get();
	}

	if (!bIntact)
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Seam link broke: %s -> %s (expected %s)"),
			*GetNameSafe(Connection), *GetNameSafe(Connection->GetConnection()), *GetNameSafe(Peer));
	}
	return bIntact;
}
