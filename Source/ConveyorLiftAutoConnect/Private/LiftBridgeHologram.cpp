#include "LiftBridgeHologram.h"

#include "Buildables/FGBuildablePassthrough.h"
#include "ConveyorLiftAutoConnectModule.h"
#include "FGConstructDisqualifier.h"
#include "FGRecipe.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "SeamLinker.h"

const FName& FLiftBridgeHologram::SyntheticBridgeTag()
{
	static const FName Tag(TEXT("ConveyorLiftAutoConnect.SyntheticBridge"));
	return Tag;
}

const TCHAR* FLiftBridgeHologram::ChildNamePrefix = TEXT("LiftAutoConnectBridge_");

AFGConveyorLiftHologram* FLiftBridgeHologram::FindOrSpawn(AFGBlueprintHologram* Parent, int32 Slot, TSubclassOf<UFGRecipe> Recipe)
{
	if (!IsValid(Parent) || !Recipe)
	{
		return nullptr;
	}

	// Child names stay registered on the parent after Destroy(), so each slot and recipe keeps one child for the
	// parent's lifetime. The path hash keeps recipes with equal short names apart.
	const FString RecipePath = Recipe->GetPathName();
	const FName ChildName(*FString::Printf(TEXT("%s%d_%s_%s"),
		ChildNamePrefix, Slot, *GetNameSafe(Recipe.Get()), *FMD5::HashAnsiString(*RecipePath)));

	AFGHologram* Child = Parent->FindChildHologramByName(ChildName);
	if (Child == nullptr)
	{
		AActor* Owner = IsValid(Parent->GetOwner()) ? Parent->GetOwner() : Parent;
		Child = AFGHologram::SpawnChildHologramFromRecipe(Parent, ChildName, Recipe, Owner, Parent->GetActorLocation());
	}
	else if (!IsValid(Child))
	{
		UE_LOG(LogConveyorLiftAutoConnect, Warning,
			TEXT("Bridge child %s is pending destruction; not respawning it under the same name (recipe %s)"),
			*ChildName.ToString(), *RecipePath);
		return nullptr;
	}

	AFGConveyorLiftHologram* Bridge = Cast<AFGConveyorLiftHologram>(Child);
	if (!IsValid(Bridge))
	{
		if (IsValid(Child))
		{
			Child->SetDisabled(true);
		}
		UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("Recipe %s does not make a conveyor lift hologram"), *RecipePath);
		return nullptr;
	}

	Bridge->SetShouldSpawnChildHolograms(false);
	Bridge->SetBuildModeOverride(Parent->GetBridgeHologramBuildModeOverride());
	Bridge->SetDisabled(true);
	return Bridge;
}

bool FLiftBridgeHologram::Configure(
	AFGConveyorLiftHologram* Bridge,
	const FLiftEndpoint& Blueprint,
	const FLiftEndpoint& Target,
	ELiftFlow Flow,
	bool bFinal)
{
	if (!IsValid(Bridge) || !Blueprint.HasBuildable() || !Target.HasBuildable() || Flow == ELiftFlow::Unknown)
	{
		return false;
	}

	const bool bBlueprintLower = Blueprint.GetLocation().Z <= Target.GetLocation().Z;
	const FLiftEndpoint& Lower = bBlueprintLower ? Blueprint : Target;
	const FLiftEndpoint& Upper = bBlueprintLower ? Target : Blueprint;
	const FLiftEndpoint& Input = Flow == ELiftFlow::Up ? Lower : Upper;
	const FLiftEndpoint& Output = Flow == ELiftFlow::Up ? Upper : Lower;

	// Vanilla starts a lift at the attachment when one end is an attachment, otherwise at the transport input.
	const bool bStartAtOutput =
		Input.Kind != ELiftEndpointKind::AttachmentPort && Output.Kind == ELiftEndpointKind::AttachmentPort;
	const FLiftEndpoint& Start = bStartAtOutput ? Output : Input;
	const FLiftEndpoint& End = bStartAtOutput ? Input : Output;
	const bool bStartsAtAttachment = Start.Kind == ELiftEndpointKind::AttachmentPort;
	const bool bEndsAtAttachment = End.Kind == ELiftEndpointKind::AttachmentPort;

	const FVector Heading = ResolveHeading(Blueprint, Target);
	FRotator Rotation = Heading.Rotation();
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	const FTransform StartTransform(Rotation, Start.GetLocation(), FVector::OneVector);
	const FTransform EndTransform(Rotation, End.GetLocation(), FVector::OneVector);

	Bridge->SetActorTransform(StartTransform);
	Bridge->mTopTransform = EndTransform.GetRelativeTransform(StartTransform);
	Bridge->mSnappedPassthroughs.SetNum(2);

	// Vanilla snapped state, measured from manual placement.
	EFactoryConnectionDirection StartPortDirection = EFactoryConnectionDirection::FCD_ANY;
	if (bStartsAtAttachment)
	{
		UFGFactoryConnectionComponent* StartPort = Start.GetPort();
		if (!IsValid(StartPort) || !PrepareAttachmentDirection(Start, !bStartAtOutput, Blueprint, bFinal, StartPortDirection))
		{
			return false;
		}

		AFGBuildablePassthrough* EndHole = nullptr;
		UFGFactoryConnectionComponent* EndPort = nullptr;
		if (bEndsAtAttachment)
		{
			EFactoryConnectionDirection EndPortDirection;
			EndPort = End.GetPort();
			if (!IsValid(EndPort) ||
				!PrepareAttachmentDirection(End, bStartAtOutput, Blueprint, bFinal, EndPortDirection) ||
				LiftAutoConnect::Opposite(EndPortDirection) != StartPortDirection)
			{
				return false;
			}
		}
		else
		{
			EndHole = End.GetFloorHole();
			if (!LiftAutoConnect::IsConveyorFloorHole(EndHole))
			{
				return false;
			}
		}

		Bridge->mSnappedPassthroughs[0] = nullptr;
		Bridge->mSnappedPassthroughs[1] = EndHole;
		Bridge->mSnappedConnectionComponents[0] = StartPort;
		Bridge->mSnappedConnectionComponents[1] = bEndsAtAttachment ? EndPort : End.GetContinuation();
		Bridge->mForcedNormalDirection = Start.GetOutwardNormal();
		Bridge->mArrowDirection = StartPortDirection;
		Bridge->mIsReversed = StartPortDirection == EFactoryConnectionDirection::FCD_INPUT;
		Bridge->mFirstStepYaw = 0.0f;
	}
	else
	{
		Bridge->mSnappedPassthroughs[0] = Input.GetFloorHole();
		Bridge->mSnappedPassthroughs[1] = Output.GetFloorHole();
		Bridge->mSnappedConnectionComponents[0] = nullptr;
		Bridge->mSnappedConnectionComponents[1] = nullptr;
		Bridge->mForcedNormalDirection = Heading;
		Bridge->mArrowDirection = EFactoryConnectionDirection::FCD_INPUT;
		Bridge->mIsReversed = false;
		Bridge->mFirstStepYaw = Rotation.Yaw;
	}
	Bridge->mActivePointIdx = 1;

	Bridge->Tags.AddUnique(SyntheticBridgeTag());
	ON_SCOPE_EXIT
	{
		if (bFinal)
		{
			Bridge->Tags.Remove(SyntheticBridgeTag());
		}
	};

	Bridge->SetDisabled(false);
	Bridge->ResetConstructDisqualifiers();
	Bridge->UpdateConnectionDirections();
	Bridge->OnRep_TopTransform();
	Bridge->OnRep_SnappedPassthroughs();

	if (bStartsAtAttachment)
	{
		// The calls above reset the transport state of a synthetic attachment-first lift; apply it again.
		UFGFactoryConnectionComponent* Own0 = Bridge->mConnectionComponents[0].Get();
		UFGFactoryConnectionComponent* Own1 = Bridge->mConnectionComponents[1].Get();
		if (!IsValid(Own0) || !IsValid(Own1))
		{
			return false;
		}
		Own0->SetDirection(LiftAutoConnect::Opposite(StartPortDirection));
		Own1->SetDirection(StartPortDirection);
		Bridge->mArrowDirection = StartPortDirection;
		Bridge->mIsReversed = StartPortDirection == EFactoryConnectionDirection::FCD_INPUT;
	}

	if (!CanConnectToSecondEnd(Bridge, End))
	{
		return false;
	}

	Bridge->OnRep_ArrowDirection();
	Bridge->UpdateClearance();
	if (!bFinal)
	{
		Bridge->CheckBlueprintCommingling();
	}
	Bridge->CheckValidPlacement();
	const bool bCanConstruct = Bridge->CanConstruct();

	if (bFinal)
	{
		TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
		Bridge->GetConstructDisqualifiers(Disqualifiers);
		FString DisqualifierNames;
		for (const TSubclassOf<UFGConstructDisqualifier>& Disqualifier : Disqualifiers)
		{
			DisqualifierNames += (DisqualifierNames.IsEmpty() ? TEXT("") : TEXT(",")) + GetNameSafe(Disqualifier.Get());
		}
		UE_LOG(LogConveyorLiftAutoConnect, Verbose,
			TEXT("Final bridge %s flow=%s input=%s output=%s start=%s span=%.1f canConstruct=%d disqualifiers=[%s]"),
			*GetNameSafe(Bridge), LiftAutoConnect::ToString(Flow), *Input.Describe(), *Output.Describe(), *Start.Describe(),
			Bridge->mTopTransform.GetTranslation().Z, bCanConstruct ? 1 : 0, *DisqualifierNames);
	}
	return bCanConstruct;
}

FVector FLiftBridgeHologram::ResolveHeading(const FLiftEndpoint& Blueprint, const FLiftEndpoint& Target)
{
	FVector Heading = Blueprint.GetAdjacentLiftHeading();
	if (Heading.IsNearlyZero())
	{
		Heading = Target.GetAdjacentLiftHeading();
	}

	AFGBuildablePassthrough* Hole = Blueprint.GetFloorHole();
	FVector AttachDirection = FVector::ZeroVector;
	if (Heading.IsNearlyZero() && IsValid(Hole) && Hole->GetValidAttachDirection(AttachDirection) && !AttachDirection.IsNearlyZero())
	{
		const FVector LocalDirection = Hole->GetActorTransform().InverseTransformVectorNoScale(AttachDirection);
		Heading = LiftAutoConnect::GetBuildableTransform(Blueprint.Buildable, Blueprint.Preview).TransformVectorNoScale(LocalDirection);
	}
	if (Heading.IsNearlyZero())
	{
		Heading = LiftAutoConnect::GetBuildableTransform(Blueprint.Buildable, Blueprint.Preview).GetRotation().GetForwardVector();
	}

	Heading.Z = 0.0f;
	return Heading.IsNearlyZero() ? FVector::ForwardVector : Heading.GetSafeNormal();
}

bool FLiftBridgeHologram::PrepareAttachmentDirection(
	const FLiftEndpoint& Attachment,
	bool bTransportInput,
	const FLiftEndpoint& Blueprint,
	bool bFinal,
	EFactoryConnectionDirection& OutDirection)
{
	// The attachment feeds the lift at the transport input and takes from it at the output.
	OutDirection = bTransportInput ? EFactoryConnectionDirection::FCD_OUTPUT : EFactoryConnectionDirection::FCD_INPUT;
	UFGFactoryConnectionComponent* Port = Attachment.GetPort();
	if (!IsValid(Port))
	{
		return false;
	}
	if (bFinal && Attachment.Buildable == Blueprint.Buildable &&
		!FSeamLinker::RestoreSavedAttachmentDirection(Attachment, OutDirection, nullptr))
	{
		return false;
	}
	const EFactoryConnectionDirection Current = Port->GetDirection();
	return !LiftAutoConnect::IsTransportDirection(Current) || Current == OutDirection;
}

bool FLiftBridgeHologram::CanConnectToSecondEnd(AFGConveyorLiftHologram* Bridge, const FLiftEndpoint& SecondEnd)
{
	if (SecondEnd.Kind == ELiftEndpointKind::FloorHole)
	{
		return LiftAutoConnect::IsConveyorFloorHole(SecondEnd.GetFloorHole());
	}

	// Vanilla rejects a lift ending at a second attachment; a mod extending CanConnectToConnection can allow it.
	UFGFactoryConnectionComponent* LiftConnection = Bridge->mConnectionComponents[1].Get();
	UFGFactoryConnectionComponent* Port = SecondEnd.GetPort();
	UFGFactoryConnectionComponent* AttachmentConnection = SecondEnd.Preview ? SecondEnd.GetRepresentationConnection() : Port;
	if (!IsValid(LiftConnection) || !IsValid(AttachmentConnection))
	{
		return false;
	}

	// Both components must sit where the lift will end for the check; restore them right after.
	const FTransform EndTransform = SecondEnd.GetWorldTransform();
	const bool bStagePort = SecondEnd.Preview && AttachmentConnection == Port;
	const FTransform LiftConnectionRelative = LiftConnection->GetRelativeTransform();
	const FTransform AttachmentRelative = AttachmentConnection->GetRelativeTransform();
	if (LiftConnection->GetMobility() != EComponentMobility::Movable)
	{
		LiftConnection->SetMobility(EComponentMobility::Movable);
	}
	if (bStagePort)
	{
		if (AttachmentConnection->GetMobility() != EComponentMobility::Movable)
		{
			AttachmentConnection->SetMobility(EComponentMobility::Movable);
		}
		AttachmentConnection->SetWorldTransform(EndTransform);
	}
	LiftConnection->SetWorldLocation(EndTransform.GetLocation());

	const bool bCanConnect = Bridge->CanConnectToConnection(LiftConnection, AttachmentConnection);

	LiftConnection->SetRelativeTransform(LiftConnectionRelative);
	if (bStagePort)
	{
		AttachmentConnection->SetRelativeTransform(AttachmentRelative);
	}
	return bCanConnect;
}
