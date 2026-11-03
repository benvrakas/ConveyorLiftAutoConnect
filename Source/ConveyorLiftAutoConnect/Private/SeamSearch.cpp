#include "SeamSearch.h"

namespace
{
	// Largest offset, in cm, between two endpoint positions that count as the same; covers single-precision rounding
	// of saved and placed positions (up to 2.4 mm seen between stacked blueprints).
	constexpr double PositionTolerance = 2.0;

	bool IsSameColumn(const FVector& First, const FVector& Second)
	{
		return FMath::Abs(First.X - Second.X) <= PositionTolerance && FMath::Abs(First.Y - Second.Y) <= PositionTolerance;
	}

	bool IsSameHeight(double First, double Second)
	{
		return FMath::Abs(First - Second) <= PositionTolerance;
	}

	// A blueprint endpoint of another buildable strictly between the source and target heights in the source's column.
	const FLocatedEndpoint* FindBlocker(
		const FLiftEndpoint& Source,
		const FVector& SourceLocation,
		const FVector& TargetLocation,
		const TArray<FLocatedEndpoint>& BlueprintEndpoints)
	{
		const double MinZ = FMath::Min(SourceLocation.Z, TargetLocation.Z);
		const double MaxZ = FMath::Max(SourceLocation.Z, TargetLocation.Z);
		if (!(MinZ < MaxZ))
		{
			return nullptr;
		}
		for (const FLocatedEndpoint& Blocker : BlueprintEndpoints)
		{
			if (Blocker.Endpoint.Buildable == Source.Buildable || !IsSameColumn(Blocker.Location, SourceLocation))
			{
				continue;
			}
			if (Blocker.Location.Z > MinZ + PositionTolerance && Blocker.Location.Z < MaxZ - PositionTolerance)
			{
				return &Blocker;
			}
		}
		return nullptr;
	}

	bool IsCandidateBefore(const FSeamCandidate& First, const FSeamCandidate& Second)
	{
		if (!IsSameHeight(First.Distance, Second.Distance))
		{
			return First.Distance < Second.Distance;
		}
		const FString FirstPath = IsValid(First.Target.Buildable) ? First.Target.Buildable->GetPathName() : FString();
		const FString SecondPath = IsValid(Second.Target.Buildable) ? Second.Target.Buildable->GetPathName() : FString();
		if (FirstPath != SecondPath)
		{
			return FirstPath.Compare(SecondPath, ESearchCase::CaseSensitive) < 0;
		}
		if (First.Target.Kind != Second.Target.Kind)
		{
			return First.Target.Kind < Second.Target.Kind;
		}
		if (First.Target.Facing != Second.Target.Facing)
		{
			return First.Target.Facing < Second.Target.Facing;
		}
		return First.Target.Port.ToString().Compare(Second.Target.Port.ToString(), ESearchCase::CaseSensitive) < 0;
	}
}

ESeamGeometry SeamSearch::Classify(
	const FLiftEndpoint& Source,
	const FVector& SourceLocation,
	const FLiftEndpoint& Target,
	const FVector& TargetLocation)
{
	if (!IsSameColumn(SourceLocation, TargetLocation))
	{
		return ESeamGeometry::None;
	}

	// An attachment port only takes lifts on its own side, unless the two endpoints are level.
	const bool bLevel = IsSameHeight(SourceLocation.Z, TargetLocation.Z);
	auto PortFacesOther = [bLevel](const FLiftEndpoint& Endpoint, double OwnZ, double OtherZ)
	{
		return Endpoint.Kind != ELiftEndpointKind::AttachmentPort || bLevel ||
			Endpoint.Facing == (OwnZ < OtherZ ? ELiftFacing::Up : ELiftFacing::Down);
	};
	if (!PortFacesOther(Source, SourceLocation.Z, TargetLocation.Z) || !PortFacesOther(Target, TargetLocation.Z, SourceLocation.Z))
	{
		return ESeamGeometry::None;
	}

	const FVector Delta = TargetLocation - SourceLocation;
	if (bLevel)
	{
		const FVector SourceNormal = Source.GetOutwardNormal().GetSafeNormal();
		const FVector TargetNormal = Target.GetOutwardNormal().GetSafeNormal();
		return SourceNormal.Equals(-TargetNormal) ? ESeamGeometry::Direct : ESeamGeometry::None;
	}

	const bool bTargetAbove = Delta.Z > 0.0;
	const bool bFacing = bTargetAbove
		? Source.Facing == ELiftFacing::Up && Target.Facing == ELiftFacing::Down
		: Source.Facing == ELiftFacing::Down && Target.Facing == ELiftFacing::Up;
	return bFacing ? ESeamGeometry::Span : ESeamGeometry::None;
}

void SeamSearch::FindNearestPlane(
	const FLiftEndpoint& Source,
	const FVector& SourceLocation,
	const TArray<FLocatedEndpoint>& Targets,
	const TArray<FLocatedEndpoint>& BlueprintEndpoints,
	TArray<FSeamCandidate>& OutCandidates,
	FLiftEndpoint* OutBlocker)
{
	OutCandidates.Reset();
	bool bReportedBlocker = false;
	for (const FLocatedEndpoint& Target : Targets)
	{
		if (!Target.Endpoint.HasBuildable())
		{
			continue;
		}
		const ESeamGeometry Geometry = Classify(Source, SourceLocation, Target.Endpoint, Target.Location);
		if (Geometry == ESeamGeometry::None)
		{
			continue;
		}
		if (Geometry == ESeamGeometry::Span)
		{
			if (const FLocatedEndpoint* Blocker = FindBlocker(Source, SourceLocation, Target.Location, BlueprintEndpoints))
			{
				if (OutBlocker && !bReportedBlocker)
				{
					*OutBlocker = Blocker->Endpoint;
					bReportedBlocker = true;
				}
				continue;
			}
		}
		OutCandidates.Add({Target.Endpoint, static_cast<float>(FMath::Abs(Target.Location.Z - SourceLocation.Z)), Geometry});
	}

	OutCandidates.Sort(IsCandidateBefore);
	if (OutCandidates.Num() > 0)
	{
		const float Nearest = OutCandidates[0].Distance;
		const int32 FirstFarther = OutCandidates.IndexOfByPredicate([Nearest](const FSeamCandidate& Candidate)
		{
			return !IsSameHeight(Candidate.Distance, Nearest);
		});
		if (FirstFarther != INDEX_NONE)
		{
			OutCandidates.SetNum(FirstFarther);
		}
	}
}

ELiftFlow SeamSearch::ResolveFlow(const FLiftEndpoint& First, const FLiftEndpoint& Second)
{
	const ELiftFlow FirstFlow = First.GetImpliedFlow();
	const ELiftFlow SecondFlow = Second.GetImpliedFlow();
	if (FirstFlow != ELiftFlow::Unknown && SecondFlow != ELiftFlow::Unknown && FirstFlow != SecondFlow)
	{
		return ELiftFlow::Unknown;
	}
	return FirstFlow != ELiftFlow::Unknown ? FirstFlow : SecondFlow;
}

bool SeamSearch::CanLinkDirectly(
	UFGFactoryConnectionComponent* Source,
	UFGFactoryConnectionComponent* Target,
	EFactoryConnectionDirection& OutSourceDirection,
	EFactoryConnectionDirection& OutTargetDirection)
{
	OutSourceDirection = EFactoryConnectionDirection::FCD_ANY;
	OutTargetDirection = EFactoryConnectionDirection::FCD_ANY;
	if (!IsValid(Source) || !IsValid(Target))
	{
		return false;
	}

	const bool bLinked = Source->GetConnection() == Target && Target->GetConnection() == Source &&
		Source->IsConnected() && Target->IsConnected();
	const bool bFree = Source->GetConnection() == nullptr && Target->GetConnection() == nullptr &&
		!Source->IsConnected() && !Target->IsConnected();
	if (!bLinked && !bFree)
	{
		return false;
	}

	OutSourceDirection = Source->GetDirection();
	OutTargetDirection = Target->GetDirection();
	if (!LiftAutoConnect::IsTransportDirection(OutSourceDirection) && !LiftAutoConnect::IsTransportDirection(OutTargetDirection))
	{
		return false;
	}
	if (!LiftAutoConnect::IsTransportDirection(OutSourceDirection))
	{
		OutSourceDirection = LiftAutoConnect::Opposite(OutTargetDirection);
	}
	if (!LiftAutoConnect::IsTransportDirection(OutTargetDirection))
	{
		OutTargetDirection = LiftAutoConnect::Opposite(OutSourceDirection);
	}
	if (LiftAutoConnect::Opposite(OutSourceDirection) != OutTargetDirection)
	{
		return false;
	}
	return bLinked || Source->CanConnectTo(Target) || Target->CanConnectTo(Source);
}
