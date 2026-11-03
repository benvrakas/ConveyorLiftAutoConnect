#pragma once

#include "CoreMinimal.h"
#include "FGFactoryConnectionComponent.h"

class AFGBlueprintHologram;
class AFGBuildable;
class AFGBuildableConveyorLift;
class AFGBuildablePassthrough;
class UFGRecipe;

enum class ELiftEndpointKind : uint8
{
	FloorHole,
	AttachmentPort
};

// Vertical direction in which a lift leaves an endpoint.
enum class ELiftFacing : uint8
{
	Up,
	Down
};

// Item flow through a lift.
enum class ELiftFlow : uint8
{
	Unknown,
	Up,
	Down
};

namespace LiftAutoConnect
{
	bool IsTransportDirection(EFactoryConnectionDirection Direction);
	EFactoryConnectionDirection Opposite(EFactoryConnectionDirection Direction);
	ELiftFacing Opposite(ELiftFacing Facing);
	bool IsConveyorFloorHole(const AFGBuildablePassthrough* Hole);
	// The connection snapped to the hole on the given side (Up = top).
	UFGFactoryConnectionComponent* GetFloorHoleSnapped(AFGBuildablePassthrough* Hole, ELiftFacing Side);
	void SetFloorHoleSnapped(AFGBuildablePassthrough* Hole, ELiftFacing Side, UFGFactoryConnectionComponent* Connection);
	// World transform of a buildable, or of its preview when it belongs to a blueprint hologram.
	FTransform GetBuildableTransform(const AFGBuildable* Buildable, const AFGBlueprintHologram* Preview);
	const TCHAR* ToString(ELiftFlow Flow);
	// Standalone, client, listen server or dedicated server, for logs from multiplayer sessions.
	const TCHAR* DescribeNetMode(const UObject* WorldContext);
}

// One place a conveyor lift can end: a side of a conveyor floor hole, or a vertical lift port of a conveyor attachment.
class FLiftEndpoint
{
public:
	AFGBuildable* Buildable = nullptr;
	ELiftEndpointKind Kind = ELiftEndpointKind::FloorHole;
	ELiftFacing Facing = ELiftFacing::Up;
	// Attachment lift port component name; None for a floor hole.
	FName Port;
	// The blueprint hologram previewing Buildable, or null for a buildable in the world.
	AFGBlueprintHologram* Preview = nullptr;

	static bool IsSupportedBuildable(const AFGBuildable* Buildable);
	// Appends the buildable's endpoints in a fixed order: floor hole bottom then top, attachment upward port then downward.
	static void Gather(AFGBuildable* Buildable, AFGBlueprintHologram* Preview, bool bOpenOnly, TArray<FLiftEndpoint>& OutEndpoints);

	bool HasBuildable() const;
	bool IsOpen() const;
	FTransform GetWorldTransform() const;
	FVector GetLocation() const;
	// Direction a connection at this endpoint faces: the side normal for a floor hole, the port's forward vector otherwise.
	FVector GetOutwardNormal() const;
	AFGBuildablePassthrough* GetFloorHole() const;
	UFGFactoryConnectionComponent* GetPort() const;
	// The connection a lift at this endpoint links to: the conveyor across the floor hole, or the attachment port itself.
	UFGFactoryConnectionComponent* GetContinuation() const;
	// For a blueprint endpoint, the hologram's duplicate of the continuation that carries its connection indicator.
	UFGFactoryConnectionComponent* GetRepresentationConnection() const;
	AFGBuildableConveyorLift* GetAdjacentLift() const;
	TSubclassOf<UFGRecipe> GetAdjacentLiftRecipe() const;
	// Horizontal heading of the adjacent lift, or zero without one.
	FVector GetAdjacentLiftHeading() const;
	// Flow a lift here must have to match the continuation's direction; Unknown when the continuation has none.
	ELiftFlow GetImpliedFlow() const;
	// The same endpoint on the buildable constructed from this blueprint buildable.
	FLiftEndpoint InWorld(AFGBuildable* Constructed) const;
	FString Describe() const;

	bool operator==(const FLiftEndpoint& Other) const
	{
		return Buildable == Other.Buildable && Kind == Other.Kind && Facing == Other.Facing && Port == Other.Port;
	}

	friend uint32 GetTypeHash(const FLiftEndpoint& Endpoint)
	{
		uint32 Hash = HashCombine(GetTypeHash(Endpoint.Buildable), GetTypeHash(static_cast<uint8>(Endpoint.Kind)));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Endpoint.Facing)));
		return HashCombine(Hash, GetTypeHash(Endpoint.Port));
	}
};
