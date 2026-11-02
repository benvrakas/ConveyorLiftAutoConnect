#pragma once

#include "CoreMinimal.h"
#include "Hologram/FGBlueprintOpenConnectionManager.h"
#include "LiftEndpoint.h"
#include "SeamLinker.h"
#include "SeamSearch.h"

class AFGConveyorLiftHologram;

// Connects a blueprint's open vertical conveyor endpoints to the nearest aligned endpoints in the world, with a
// generated Conveyor Lift or, where they touch, by linking their conveyors directly.
class FLiftAutoConnectManager final : public FGBlueprintOpenConnectionManagerBase
{
public:
	explicit FLiftAutoConnectManager(AFGBlueprintHologram* BlueprintHologram);

	virtual void RegisterNearbyActor(AActor* Actor) override;
	virtual void UnregisterNearbyActor(AActor* Actor) override;
	virtual void Initialize(TArray<AFGBuildable*> Buildables) override;
	virtual void UpdateAutomaticConnections(const FHitResult& HitResult, bool& OutPlaySnapEffects) override;
	virtual bool AttemptConnectionStateSnap() override;
	virtual bool CanSnapConnectionStates() const override;
	virtual void ResetAutomaticConnections() override;
	virtual void HandleBuildableConnectionRemapping(AFGBuildable* Buildable, int32 BlueprintBuildableIndex) override;
	virtual void SerializeConstructMessage(FArchive& Archive, FNetConstructionID Id) override;
	virtual void PostConstructMessageDeserialization() override;
	virtual void Construct(TArray<AFGBuildable*>& OutConstructedBridgeBuildables, FNetConstructionID NetConstructionID) override;

private:
	// Inputs of a bridge's last successful preview configuration.
	struct FBridgeConfigKey
	{
		AFGConveyorLiftHologram* Bridge = nullptr;
		TSubclassOf<UFGRecipe> Recipe;
		FLiftEndpoint Target;
		FTransform SourceTransform;
		FTransform TargetTransform;
		ELiftFlow Flow = ELiftFlow::Unknown;

		bool operator==(const FBridgeConfigKey& Other) const;
	};

	// One open endpoint of the blueprint and the seam chosen for it.
	struct FSeamState
	{
		FLiftEndpoint Source;
		int32 SourceIndex = INDEX_NONE;
		AFGBuildable* ConstructedSource = nullptr;

		// The lift behind a source floor hole, as blueprint buildable index and connection slot.
		bool bHadContinuation = false;
		int32 ContinuationIndex = INDEX_NONE;
		int32 ContinuationSlot = INDEX_NONE;
		UFGFactoryConnectionComponent* ConstructedContinuation = nullptr;

		// The chosen seam, sent with the construct message.
		TWeakObjectPtr<AFGBuildable> TargetBuildable;
		ELiftEndpointKind TargetKind = ELiftEndpointKind::FloorHole;
		ELiftFacing TargetFacing = ELiftFacing::Up;
		FName TargetPort;
		TSubclassOf<UFGRecipe> Recipe;
		ELiftFlow Flow = ELiftFlow::Unknown;
		bool bDirect = false;
		EFactoryConnectionDirection DirectSourceDirection = EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection DirectTargetDirection = EFactoryConnectionDirection::FCD_ANY;
		bool bSnapped = false;
		TObjectPtr<AFGConveyorLiftHologram> Bridge = nullptr;

		bool bValid = false;
		TOptional<FBridgeConfigKey> ConfiguredBridge;
		// Last logged reason this state found no seam, with digits masked.
		FString LastRefusal;

		FLiftEndpoint GetTarget() const;
		void SetTarget(const FLiftEndpoint& Target);
		// Clears the chosen seam; keeps the bridge hologram and its recipe for reuse.
		void ClearTarget();
		FBlueprintContinuation GetBlueprintContinuation() const;
	};

	void GatherTargets(TArray<FLocatedEndpoint>& OutTargets) const;
	void UpdateState(
		FSeamState& State,
		int32 StateIndex,
		const TSet<FLiftEndpoint>& Claimed,
		const TArray<FLocatedEndpoint>& Targets,
		const TArray<FLocatedEndpoint>& Blockers);
	// Blueprint side's adjacent lift tier first, then the target side's.
	static TSubclassOf<UFGRecipe> ResolveRecipe(const FLiftEndpoint& Source, const FLiftEndpoint& Target);
	bool EnsureBridge(FSeamState& State, int32 StateIndex, TSubclassOf<UFGRecipe> Recipe);
	bool ConfigurePreviewBridge(FSeamState& State);
	static void DisableBridge(FSeamState& State);
	void BroadcastStateChange(const FSeamState& State, const FLiftEndpoint& PreviousTarget);
	void ConstructSeam(FSeamState& State, TArray<AFGBuildable*>& OutConstructed, FNetConstructionID NetConstructionID);

	TArray<FSeamState> States;
	// Every vertical endpoint of the blueprint, open or not; spans may not cross them.
	TArray<FLiftEndpoint> BlueprintEndpoints;
	TArray<TWeakObjectPtr<AFGBuildable>> NearbyBuildables;
	TSet<int32> ReportedBlockedStates;

	static constexpr int32 UpdateTimingInterval = 300;
	int32 UpdateTimingCount = 0;
	double UpdateTimingTotalSeconds = 0.0;
	double UpdateTimingMaxSeconds = 0.0;
};
