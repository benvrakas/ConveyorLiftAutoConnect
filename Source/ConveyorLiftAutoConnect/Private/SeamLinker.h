#pragma once

#include "CoreMinimal.h"
#include "LiftEndpoint.h"

class AFGBuildableConveyorBase;
class AFGBuildableConveyorLift;

// Records connection changes so a failed seam can be undone.
class FSeamTransaction
{
public:
	void SetDirection(UFGFactoryConnectionComponent* Connection, EFactoryConnectionDirection Direction);
	void SetFloorHoleSnapped(AFGBuildablePassthrough* Hole, ELiftFacing Side, UFGFactoryConnectionComponent* Connection);
	// Links the connections from whichever side accepts; returns whether they now point at each other.
	bool Link(UFGFactoryConnectionComponent* First, UFGFactoryConnectionComponent* Second);
	// Undoes the recorded changes newest first, skipping any the game has changed since.
	void Rollback();

private:
	TArray<TFunction<void()>> UndoSteps;
};

// The conveyor behind a blueprint floor hole, recorded at preview and located again on the constructed buildables.
struct FBlueprintContinuation
{
	bool bPresent = false;
	UFGFactoryConnectionComponent* Constructed = nullptr;
};

// Endpoints touching with opposing normals, linked without a lift. Blueprint is the constructed endpoint.
struct FDirectSeam
{
	FLiftEndpoint Blueprint;
	FLiftEndpoint Target;
	EFactoryConnectionDirection BlueprintDirection = EFactoryConnectionDirection::FCD_ANY;
	EFactoryConnectionDirection TargetDirection = EFactoryConnectionDirection::FCD_ANY;
	FBlueprintContinuation Continuation;
};

// Endpoints joined by a generated lift. Blueprint is the constructed endpoint.
struct FBridgeSeam
{
	FLiftEndpoint Blueprint;
	FLiftEndpoint Target;
	ELiftFlow Flow = ELiftFlow::Unknown;
	FBlueprintContinuation Continuation;
};

// One end of a generated lift.
struct FBridgeEnd
{
	FLiftEndpoint Endpoint;
	// The connection the lift links to here; null at a bare floor hole.
	UFGFactoryConnectionComponent* Outside = nullptr;
	// Direction of the lift's own connection here.
	EFactoryConnectionDirection LiftDirection = EFactoryConnectionDirection::FCD_ANY;
	UFGFactoryConnectionComponent* LiftConnection = nullptr;
	bool bAlreadyLinked = false;
};

struct FBridgePlan
{
	FBridgeEnd Input;
	FBridgeEnd Output;
	ELiftFlow Flow = ELiftFlow::Unknown;
};

// Links seams after the blueprint is constructed, before its buildables run BeginPlay.
// Friend of AFGBuildableConveyorAttachment, AFGBuildableConveyorLift and AFGBuildableSubsystem.
class FSeamLinker
{
public:
	// Sets an attachment port to its saved direction, which must equal Expected. Before BeginPlay a newly built
	// attachment can report a stale direction.
	static bool RestoreSavedAttachmentDirection(
		const FLiftEndpoint& Port,
		EFactoryConnectionDirection Expected,
		FSeamTransaction* Transaction);

	static bool LinkDirect(const FDirectSeam& Seam);
	// Checks both ends before the lift is constructed.
	static bool PlanBridge(const FBridgeSeam& Seam, FBridgePlan& OutPlan);
	// Links the constructed lift into the plan's ends; on failure leaves the lift detached for destruction.
	static bool LinkBridge(AFGBuildableConveyorLift* Lift, FBridgePlan& Plan);
	// Re-registers a linked conveyor so it shares its neighbors' conveyor bucket and chain actor.
	static void Regroup(AFGBuildableConveyorBase* Conveyor);

private:
	static bool CheckBridgeEnd(const FBridgeSeam& Seam, const TCHAR* Name, FBridgeEnd& End);
	static bool PrepareLiftEnd(const TCHAR* Name, FBridgeEnd& End);
	static bool IsBridgeEndLinked(const TCHAR* Name, const FBridgeEnd& End);
	static void DetachLift(AFGBuildableConveyorLift* Lift, const FBridgePlan& Plan, FSeamTransaction& Transaction);
	static bool IsLiftConnection(UFGFactoryConnectionComponent* Connection);
};
