#pragma once

#include "CoreMinimal.h"
#include "LiftEndpoint.h"

// An endpoint with its location, resolved once per preview update.
struct FLocatedEndpoint
{
	FLiftEndpoint Endpoint;
	FVector Location = FVector::ZeroVector;
};

enum class ESeamGeometry : uint8
{
	None,
	// The endpoints face each other along one column; a lift can join them.
	Span,
	// The endpoints touch with opposing normals; their conveyors can link without a lift.
	Direct
};

struct FSeamCandidate
{
	FLiftEndpoint Target;
	float Distance = 0.0f;
	ESeamGeometry Geometry = ESeamGeometry::None;
};

namespace SeamSearch
{
	ESeamGeometry Classify(const FLiftEndpoint& Source, const FVector& SourceLocation, const FLiftEndpoint& Target, const FVector& TargetLocation);

	// Fills OutCandidates with the targets on the nearest endpoint plane above or below Source, nearest first in a fixed order.
	// Occupied targets are included because they end the column. A span crossing another blueprint endpoint is skipped;
	// the first such endpoint is returned in OutBlocker.
	void FindNearestPlane(
		const FLiftEndpoint& Source,
		const FVector& SourceLocation,
		const TArray<FLocatedEndpoint>& Targets,
		const TArray<FLocatedEndpoint>& BlueprintEndpoints,
		TArray<FSeamCandidate>& OutCandidates,
		FLiftEndpoint* OutBlocker);

	// The flow both endpoints' continuations agree on; Unknown when they conflict or neither has one.
	ELiftFlow ResolveFlow(const FLiftEndpoint& First, const FLiftEndpoint& Second);

	// Whether two continuations are already linked to each other or can be linked, and the direction each must have.
	bool CanLinkDirectly(
		UFGFactoryConnectionComponent* Source,
		UFGFactoryConnectionComponent* Target,
		EFactoryConnectionDirection& OutSourceDirection,
		EFactoryConnectionDirection& OutTargetDirection);
}
