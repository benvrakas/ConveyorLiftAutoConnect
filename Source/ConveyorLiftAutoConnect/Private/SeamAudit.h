#pragma once

#include "CoreMinimal.h"
#include "LiftEndpoint.h"

class AFGBuildableConveyorBase;

// A connection a seam created or kept, as it must still be one tick later.
struct FSeamAuditLink
{
	TWeakObjectPtr<UFGFactoryConnectionComponent> Connection;
	// Linked peer; null when the connection must stay unlinked.
	TWeakObjectPtr<UFGFactoryConnectionComponent> Peer;
	EFactoryConnectionDirection Direction = EFactoryConnectionDirection::FCD_ANY;
	// Floor hole at this connection, and what each of its sides must hold.
	TWeakObjectPtr<AFGBuildablePassthrough> FloorHole;
	ELiftFacing FloorHoleSide = ELiftFacing::Up;
	TWeakObjectPtr<UFGFactoryConnectionComponent> SideSnapped;
	TWeakObjectPtr<UFGFactoryConnectionComponent> OppositeSnapped;
};

// Next-tick check that a seam's links held and its conveyors share one bucket and chain actor. Runs only with
// Verbose logging. Friend of AFGBuildableSubsystem.
class FSeamAudit
{
public:
	static void Schedule(
		UWorld* World,
		const FString& Label,
		TArray<FSeamAuditLink> Links,
		TArray<TWeakObjectPtr<AFGBuildableConveyorBase>> Conveyors);

private:
	static void Run(const FString& Label, const TArray<FSeamAuditLink>& Links, const TArray<TWeakObjectPtr<AFGBuildableConveyorBase>>& Conveyors);
	static bool IsLinkIntact(const FSeamAuditLink& Link);
};
