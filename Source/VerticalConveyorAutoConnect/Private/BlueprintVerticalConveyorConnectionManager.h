#pragma once

#include "CoreMinimal.h"
#include "Hologram/FGBlueprintOpenConnectionManager.h"

class AFGBuildable;
class AFGBuildableConveyorLift;
class AFGBuildablePassthrough;
class AFGConveyorLiftHologram;
class UFGFactoryConnectionComponent;
class UFGRecipe;

enum class EBlueprintVerticalEndpointKind : uint8
{
	FloorHoleSide,
	AttachmentPort
};

enum class EBlueprintVerticalEndpointSide : uint8
{
	Bottom,
	Top
};

/**
 * Adds automatic conveyor-lift bridges between compatible vertical conveyor
 * endpoints exposed by a blueprint and nearby world buildables.
 *
 * Supported endpoint kinds:
 * - Conveyor Floor Hole top/bottom sides.
 * - Canonical vertical lift ports on capability-compatible conveyor attachments.
 */
class FBlueprintVerticalConveyorConnectionManager final
	: public FGBlueprintOpenConnectionManagerBase
{
public:
	explicit FBlueprintVerticalConveyorConnectionManager(
		AFGBlueprintHologram* blueprintHologram);
	virtual ~FBlueprintVerticalConveyorConnectionManager() override = default;

	virtual void RegisterNearbyActor(AActor* actor) override;
	virtual void UnregisterNearbyActor(AActor* actor) override;
	virtual void UpdateAutomaticConnections(
		const FHitResult& hitResult,
		bool& outPlaySnapEffects) override;
	virtual void HandleBuildableConnectionRemapping(
		AFGBuildable* buildable,
		int32 blueprintBuildableIndex) override;
	virtual void Initialize(TArray<AFGBuildable*> buildables) override;
	virtual bool AttemptConnectionStateSnap() override;
	virtual bool CanSnapConnectionStates() const override;
	virtual void Construct(
		TArray<AFGBuildable*>& outConstructedBridgeBuildables,
		FNetConstructionID netConstructionID) override;
	virtual void SerializeConstructMessage(
		FArchive& archive,
		FNetConstructionID id) override;
	virtual void PostConstructMessageDeserialization() override;
	virtual void ResetAutomaticConnections() override;

private:
	struct FEndpointRef
	{
		AFGBuildable* Buildable = nullptr;
		EBlueprintVerticalEndpointKind Kind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointSide Side =
			EBlueprintVerticalEndpointSide::Top;
		FName ConnectionName = NAME_None;
		bool UsesBlueprintPreviewTransform = false;
	};

	// A world endpoint gathered once per UpdateAutomaticConnections call, with the
	// location GetEndpointWorldTransform returned for it (independent of Side).
	struct FTargetEndpointCandidate
	{
		FEndpointRef Endpoint;
		FVector Location = FVector::ZeroVector;
	};

	struct FConnectionState
	{
		AFGBuildable* BlueprintBuildable = nullptr;
		AFGBuildable* ConstructedBlueprintBuildable = nullptr;
		// A Floor Hole endpoint can obtain its lift tier and transport direction
		// from a continuation inside the blueprint. Keep an explicit locator for
		// that component so final construction can verify the Floor Hole's saved
		// passthrough pointer was remapped to the intended constructed lift.
		bool BlueprintContinuationWasPresent = false;
		int32 BlueprintContinuationBuildableIndex = INDEX_NONE;
		int32 BlueprintContinuationConnectionIndex = INDEX_NONE;
		TObjectPtr<UFGFactoryConnectionComponent>
			ConstructedBlueprintContinuationConnection = nullptr;
		TObjectPtr<AFGBuildable> TargetBuildable = nullptr;
		TObjectPtr<AFGConveyorLiftHologram> BridgeHologram = nullptr;
		TSubclassOf<UFGRecipe> LiftRecipe;
		FName BlueprintConnectionName = NAME_None;
		FName TargetConnectionName = NAME_None;
		int32 BlueprintBuildableIndex = INDEX_NONE;
		EBlueprintVerticalEndpointKind BlueprintKind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointKind TargetKind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointSide BlueprintSide =
			EBlueprintVerticalEndpointSide::Top;
		EBlueprintVerticalEndpointSide TargetSide =
			EBlueprintVerticalEndpointSide::Bottom;
		// Transport intent belongs to the snapped preview state. Constructed
		// blueprint actors may still expose FCD_ANY before BeginPlay, so final
		// construction must not rediscover an already-locked direction.
		EFactoryConnectionDirection ResolvedLowerEndpointDirection =
			EFactoryConnectionDirection::FCD_ANY;
		// A coincident endpoint pair bypasses bridge construction, but it still
		// needs deterministic transport roles after the blueprint actors are
		// remapped. Preserve the preview-resolved pair for final validation.
		EFactoryConnectionDirection DirectBlueprintConnectionDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection DirectTargetConnectionDirection =
			EFactoryConnectionDirection::FCD_ANY;
		bool HasSnappedTarget = false;
		bool CanDirectlyConnect = false;
		bool IsValid = false;

		// Inputs of the last successful preview ConfigureBridgeHologram call, so
		// an unchanged bridge is not reconfigured and revalidated every frame.
		// Local to this hologram; not part of the construct message.
		bool HasPreviewBridgeCache = false;
		TObjectPtr<AFGConveyorLiftHologram> PreviewBridgeHologram = nullptr;
		TSubclassOf<UFGRecipe> PreviewBridgeRecipe;
		FTransform PreviewBridgeBlueprintTransform;
		FTransform PreviewBridgeTargetTransform;
		AFGBuildable* PreviewBridgeTarget = nullptr;
		EBlueprintVerticalEndpointKind PreviewBridgeTargetKind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointSide PreviewBridgeBlueprintSide =
			EBlueprintVerticalEndpointSide::Top;
		EBlueprintVerticalEndpointSide PreviewBridgeTargetSide =
			EBlueprintVerticalEndpointSide::Bottom;
		FName PreviewBridgeTargetConnectionName = NAME_None;
		EFactoryConnectionDirection PreviewBridgeInputLowerDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection PreviewBridgeOutputLowerDirection =
			EFactoryConnectionDirection::FCD_ANY;
		bool PreviewBridgeSnapped = false;

		friend FArchive& operator<<(FArchive& archive, FConnectionState& state)
		{
			// BlueprintSide is normally authored by the source endpoint, but for an
			// attachment port it is geometry-derived and therefore part of the snapped
			// state that must survive client/server construct-message transfer.
			uint8 blueprintSide = static_cast<uint8>(state.BlueprintSide);
			archive << blueprintSide;
			if (archive.IsLoading())
			{
				state.BlueprintSide =
					static_cast<EBlueprintVerticalEndpointSide>(blueprintSide);
			}

			archive << state.TargetBuildable;
			archive << state.BridgeHologram;
			archive << state.LiftRecipe;
			archive << state.TargetConnectionName;

			uint8 targetKind = static_cast<uint8>(state.TargetKind);
			uint8 targetSide = static_cast<uint8>(state.TargetSide);
			archive << targetKind;
			archive << targetSide;
			if (archive.IsLoading())
			{
				state.TargetKind =
					static_cast<EBlueprintVerticalEndpointKind>(targetKind);
				state.TargetSide =
					static_cast<EBlueprintVerticalEndpointSide>(targetSide);
			}

			uint8 resolvedLowerDirection =
				static_cast<uint8>(state.ResolvedLowerEndpointDirection);
			archive << resolvedLowerDirection;
			if (archive.IsLoading())
			{
				state.ResolvedLowerEndpointDirection =
					static_cast<EFactoryConnectionDirection>(resolvedLowerDirection);
			}

			uint8 directBlueprintDirection =
				static_cast<uint8>(state.DirectBlueprintConnectionDirection);
			uint8 directTargetDirection =
				static_cast<uint8>(state.DirectTargetConnectionDirection);
			archive << directBlueprintDirection;
			archive << directTargetDirection;
			if (archive.IsLoading())
			{
				state.DirectBlueprintConnectionDirection =
					static_cast<EFactoryConnectionDirection>(
						directBlueprintDirection);
				state.DirectTargetConnectionDirection =
					static_cast<EFactoryConnectionDirection>(directTargetDirection);
			}

			archive << state.HasSnappedTarget;
			archive << state.CanDirectlyConnect;
			return archive;
		}
	};

	struct FEndpointKey
	{
		AFGBuildable* Buildable = nullptr;
		EBlueprintVerticalEndpointKind Kind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointSide Side =
			EBlueprintVerticalEndpointSide::Top;
		FName ConnectionName = NAME_None;

		bool operator==(const FEndpointKey& other) const
		{
			return Buildable == other.Buildable &&
				Kind == other.Kind &&
				Side == other.Side &&
				ConnectionName == other.ConnectionName;
		}
	};

	friend uint32 GetTypeHash(const FEndpointKey& key)
	{
		uint32 hash = HashCombine(
			GetTypeHash(key.Buildable),
			GetTypeHash(static_cast<uint8>(key.Kind)));
		hash = HashCombine(hash, GetTypeHash(static_cast<uint8>(key.Side)));
		return HashCombine(hash, GetTypeHash(key.ConnectionName));
	}

	struct FAttachmentLiftPorts
	{
		UFGFactoryConnectionComponent* Bottom = nullptr;
		UFGFactoryConnectionComponent* Top = nullptr;
	};

	struct FDirectConnectionPlan
	{
		FEndpointRef BlueprintEndpoint;
		FEndpointRef TargetEndpoint;
		UFGFactoryConnectionComponent* BlueprintConnection = nullptr;
		UFGFactoryConnectionComponent* TargetConnection = nullptr;
		UFGFactoryConnectionComponent* OriginalBlueprintExposedConnection = nullptr;
		UFGFactoryConnectionComponent* OriginalTargetExposedConnection = nullptr;
		EFactoryConnectionDirection ExpectedBlueprintDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection ExpectedTargetDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection OriginalBlueprintDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection OriginalTargetDirection =
			EFactoryConnectionDirection::FCD_ANY;
		bool WasAlreadyLinked = false;
		bool CreatedLink = false;
		bool ChangedBlueprintDirection = false;
		bool ChangedTargetDirection = false;
		bool ChangedBlueprintBookkeeping = false;
		bool ChangedTargetBookkeeping = false;
	};

	struct FDirectEndpointValidationSnapshot
	{
		TWeakObjectPtr<UFGFactoryConnectionComponent> Connection;
		TWeakObjectPtr<AFGBuildablePassthrough> FloorHole;
		EBlueprintVerticalEndpointSide FloorHoleSide =
			EBlueprintVerticalEndpointSide::Top;
		EFactoryConnectionDirection ExpectedDirection =
			EFactoryConnectionDirection::FCD_ANY;
		bool HadFloorHole = false;
	};

	struct FDirectValidationSnapshot
	{
		FDirectEndpointValidationSnapshot Blueprint;
		FDirectEndpointValidationSnapshot Target;
	};

	enum class EBridgeFinalizationStatus : uint8
	{
		Succeeded,
		InvalidLift,
		LiftStateMismatch,
		EndpointChanged,
		LinkRejected,
		PostConditionFailed
	};

	struct FBridgeEndpointPlan
	{
		FEndpointRef Endpoint;
		UFGFactoryConnectionComponent* OutsideConnection = nullptr;
		UFGFactoryConnectionComponent* BridgeConnection = nullptr;
		EFactoryConnectionDirection ExpectedBridgeDirection =
			EFactoryConnectionDirection::FCD_ANY;
		EFactoryConnectionDirection OriginalOutsideDirection =
			EFactoryConnectionDirection::FCD_ANY;
		bool HadOutsideConnection = false;
		bool WasAlreadyLinked = false;
		bool ChangedOutsideDirection = false;
	};

	struct FBridgeFinalizationPlan
	{
		FBridgeEndpointPlan Input;
		FBridgeEndpointPlan Output;
		bool ExpectedFlowsUpwards = false;
	};

	struct FBridgeFinalizationResult
	{
		EBridgeFinalizationStatus Status =
			EBridgeFinalizationStatus::InvalidLift;
		int32 InputLink = 0;
		int32 OutputLink = 0;

		bool IsSuccess() const
		{
			return Status == EBridgeFinalizationStatus::Succeeded;
		}
	};

	struct FBridgeEndpointValidationSnapshot
	{
		TWeakObjectPtr<UFGFactoryConnectionComponent> BridgeConnection;
		TWeakObjectPtr<UFGFactoryConnectionComponent> OutsideConnection;
		TWeakObjectPtr<AFGBuildablePassthrough> FloorHole;
		EBlueprintVerticalEndpointSide FloorHoleSide =
			EBlueprintVerticalEndpointSide::Top;
		EFactoryConnectionDirection ExpectedBridgeDirection =
			EFactoryConnectionDirection::FCD_ANY;
		bool HadOutsideConnection = false;
		bool HadFloorHole = false;
	};

	struct FBridgeValidationSnapshot
	{
		TWeakObjectPtr<AFGBuildableConveyorLift> Lift;
		FBridgeEndpointValidationSnapshot Input;
		FBridgeEndpointValidationSnapshot Output;
	};

	TArray<FConnectionState> ConnectionStates;
	// All managed physical vertical endpoints in the blueprint, including
	// occupied ones. These are used only as source-side tunnel blockers.
	TArray<FEndpointRef> BlueprintPhysicalEndpoints;
	TSet<int32> LoggedSourceTunnelStates;
	TArray<TWeakObjectPtr<AFGBuildable>> NearbyBuildables;
	// Verbose timing of UpdateAutomaticConnections, logged every
	// UpdateTimingLogInterval calls.
	static constexpr int32 UpdateTimingLogInterval = 300;
	int32 UpdateTimingCount = 0;
	double UpdateTimingTotalSeconds = 0.0;
	double UpdateTimingMaxSeconds = 0.0;

	static bool IsConveyorFloorHole(const AFGBuildablePassthrough* hole);
	static bool TryGetAttachmentLiftPorts(
		AFGBuildable* buildable,
		FAttachmentLiftPorts& outPorts);
	static bool IsSupportedAttachment(const AFGBuildable* buildable);
	static bool IsSupportedBuildable(const AFGBuildable* buildable);
	static EBlueprintVerticalEndpointSide OppositeSide(
		EBlueprintVerticalEndpointSide side);
	static EFactoryConnectionDirection OppositeDirection(
		EFactoryConnectionDirection direction);
	static UFGFactoryConnectionComponent* GetFloorHoleSnappedConnection(
		const AFGBuildablePassthrough* hole,
		EBlueprintVerticalEndpointSide side);
	static void SetFloorHoleSnappedConnection(
		AFGBuildablePassthrough* hole,
		EBlueprintVerticalEndpointSide side,
		UFGFactoryConnectionComponent* connection);
	static UFGFactoryConnectionComponent* FindFactoryConnection(
		AFGBuildable* buildable,
		FName connectionName);

	void GatherEndpoints(
		AFGBuildable* buildable,
		bool useBlueprintPreviewTransform,
		bool requireOpen,
		TArray<FEndpointRef>& outEndpoints) const;
	void GatherDiscoveryCandidates(
		TArray<AFGBuildable*>& outBuildables) const;
	bool IsEndpointOpen(const FEndpointRef& endpoint) const;
	bool HasInterveningBlueprintEndpoint(
		const FEndpointRef& sourceEndpoint,
		const FEndpointRef& targetEndpoint,
		FEndpointRef* outBlockingEndpoint = nullptr) const;
	FEndpointRef GetBlueprintEndpoint(
		const FConnectionState& state,
		bool useConstructedBlueprintBuildable) const;
	FEndpointRef GetTargetEndpoint(const FConnectionState& state) const;
	FEndpointKey MakeEndpointKey(const FEndpointRef& endpoint) const;
	FString DescribeEndpoint(const FEndpointRef& endpoint) const;

	bool TryGetBlueprintBuildableWorldTransform(
		const AFGBuildable* buildable,
		FTransform& outTransform) const;
	FTransform GetBuildableWorldTransform(const FEndpointRef& endpoint) const;
	FTransform GetEndpointWorldTransform(const FEndpointRef& endpoint) const;
	FVector GetEndpointOutwardNormalWorld(const FEndpointRef& endpoint) const;
	UFGFactoryConnectionComponent* GetDirectEndpointConnection(
		const FEndpointRef& endpoint) const;
	UFGFactoryConnectionComponent* GetTransportConnectionAcrossEndpoint(
		const FEndpointRef& endpoint) const;
	UFGFactoryConnectionComponent* GetBlueprintRepresentationConnection(
		const FEndpointRef& endpoint) const;
	AFGBuildableConveyorLift* GetAdjacentLift(
		const FEndpointRef& endpoint) const;
	TSubclassOf<UFGRecipe> GetEndpointLiftRecipe(
		const FEndpointRef& endpoint) const;
	EFactoryConnectionDirection GetRequiredBridgeDirection(
		const FEndpointRef& endpoint) const;

	bool RestorePersistedAttachmentDirectionBeforeConstruct(
		const FEndpointRef& attachmentEndpoint,
		EFactoryConnectionDirection expectedDirection) const;
	bool PrepareAttachmentEndpointDirection(
		const FEndpointRef& attachmentEndpoint,
		const FEndpointRef& blueprintEndpoint,
		bool isTransportInput,
		bool finalValidation,
		EFactoryConnectionDirection& outDirection) const;
	bool CanConnectLiftToPlacementEnd(
		AFGConveyorLiftHologram* bridge,
		const FEndpointRef& placementEnd) const;

	bool ResolveLiftPlacementEndpoints(
		const FEndpointRef& transportInput,
		const FEndpointRef& transportOutput,
		FEndpointRef& outPlacementStart,
		FEndpointRef& outPlacementEnd) const;
	bool ResolveLowerEndpointDirection(
		const FEndpointRef& first,
		const FEndpointRef& second,
		EFactoryConnectionDirection& outLowerEndpointDirection,
		FEndpointRef& outLower,
		FEndpointRef& outUpper) const;
	FVector GetAdjacentLiftVisualDirectionWorld(
		const FEndpointRef& endpoint) const;
	FVector ResolveBridgeVisualDirection(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& targetEndpoint) const;
	TSubclassOf<UFGRecipe> ResolveLiftRecipe(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& targetEndpoint) const;

	void SpawnBridgeHologram(FConnectionState& state, int32 stateIndex);
	bool EnsureBridgeRecipe(
		FConnectionState& state,
		int32 stateIndex,
		TSubclassOf<UFGRecipe> recipe);
	bool ConfigureBridgeHologram(
		FConnectionState& state,
		bool useConstructedBlueprintBuildable,
		bool finalValidation);
	void DisableBridge(FConnectionState& state);
	void ClearTarget(FConnectionState& state);
	void FindBestTarget(
		FConnectionState& state,
		int32 stateIndex,
		const TSet<FEndpointKey>& claimedTargetEndpoints,
		const TArray<FTargetEndpointCandidate>& targetEndpoints);
	void BroadcastConnectionStateChange(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& previousTargetEndpoint,
		const FEndpointRef& targetEndpoint,
		bool isValid);
	bool IsAttachmentPortForSpan(
		const FEndpointRef& attachmentEndpoint,
		const FEndpointRef& otherEndpoint) const;
	void NormalizeAttachmentFacingSides(
		FEndpointRef& first,
		FEndpointRef& second) const;
	bool IsGeometricallyCompatible(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& targetEndpoint,
		float& outVerticalDistance,
		bool& outCanDirectlyConnect) const;

	static bool ResolveDirectConnectionDirections(
		const UFGFactoryConnectionComponent* blueprintConnection,
		const UFGFactoryConnectionComponent* targetConnection,
		EFactoryConnectionDirection& outBlueprintDirection,
		EFactoryConnectionDirection& outTargetDirection);
	bool PrepareDirectConnectionPlan(
		const FConnectionState& state,
		FDirectConnectionPlan& outPlan) const;
	bool ApplyDirectConnectionBookkeeping(FDirectConnectionPlan& plan) const;
	bool ValidateDirectConnectionPostCondition(
		const FDirectConnectionPlan& plan) const;
	void RollBackDirectConnection(FDirectConnectionPlan& plan) const;
	bool ConnectDirectly(const FConnectionState& state) const;
	void ScheduleDirectConnectionValidation(
		const FDirectConnectionPlan& plan) const;
	static void ValidateDirectConnectionNextTick(
		FDirectValidationSnapshot snapshot);
	bool PrepareBridgeFinalizationPlan(
		FConnectionState& state,
		FBridgeFinalizationPlan& outPlan) const;
	bool PreflightBridgeEndpointBeforeConstruct(
		const FConnectionState& state,
		const TCHAR* endpointName,
		FBridgeEndpointPlan& endpointPlan) const;
	bool PrepareConstructedBridgeEndpoint(
		AFGBuildableConveyorLift* lift,
		const TCHAR* endpointName,
		UFGFactoryConnectionComponent* bridgeConnection,
		FBridgeEndpointPlan& endpointPlan) const;
	bool ApplyBridgeEndpointBookkeeping(
		AFGBuildableConveyorLift* lift,
		const TCHAR* endpointName,
		FBridgeEndpointPlan& endpointPlan) const;
	int32 ConnectBridgeEndpoint(
		AFGBuildableConveyorLift* lift,
		const TCHAR* endpointName,
		FBridgeEndpointPlan& endpointPlan) const;
	bool ValidateBridgeEndpointPostCondition(
		const TCHAR* endpointName,
		const FBridgeEndpointPlan& endpointPlan) const;
	void RollBackBridgeFinalization(
		AFGBuildableConveyorLift* lift,
		FBridgeFinalizationPlan& plan) const;
	FBridgeFinalizationResult FinalizeConstructedBridge(
		FConnectionState& state,
		FBridgeFinalizationPlan& plan,
		AFGBuildableConveyorLift* lift) const;
	void RegroupLinkedConveyor(AFGBuildableConveyorBase* conveyor) const;
	bool ConfigurePreviewBridgeHologram(FConnectionState& state);
	void SchedulePostConstructValidation(
		const FBridgeFinalizationPlan& plan,
		AFGBuildableConveyorLift* lift) const;
	static void ValidateConstructedBridgeNextTick(
		FBridgeValidationSnapshot snapshot);
};
