#pragma once

#include "CoreMinimal.h"
#include "LiftEndpoint.h"

class AFGBlueprintHologram;
class AFGConveyorLiftHologram;

// Generated Conveyor Lift holograms between blueprint and world endpoints. Friend of AFGConveyorLiftHologram.
class FLiftBridgeHologram
{
public:
	// Tag on generated bridge holograms; their transient InvalidFloor disqualifier is dropped.
	static const FName& SyntheticBridgeTag();
	// Start of every bridge child name.
	static const TCHAR* ChildNamePrefix;

	// The parent's child lift hologram for this slot and recipe, spawned disabled on first use and reused after that.
	static AFGConveyorLiftHologram* FindOrSpawn(AFGBlueprintHologram* Parent, int32 Slot, TSubclassOf<UFGRecipe> Recipe);

	// Places the hologram between the endpoints as manual vanilla placement would and runs vanilla validation.
	// Blueprint is the preview endpoint during preview and the constructed one when bFinal. Returns CanConstruct().
	static bool Configure(
		AFGConveyorLiftHologram* Bridge,
		const FLiftEndpoint& Blueprint,
		const FLiftEndpoint& Target,
		ELiftFlow Flow,
		bool bFinal);

private:
	static FVector ResolveHeading(const FLiftEndpoint& Blueprint, const FLiftEndpoint& Target);
	static bool PrepareAttachmentDirection(
		const FLiftEndpoint& Attachment,
		bool bTransportInput,
		const FLiftEndpoint& Blueprint,
		bool bFinal,
		EFactoryConnectionDirection& OutDirection);
	static bool CanConnectToSecondEnd(AFGConveyorLiftHologram* Bridge, const FLiftEndpoint& SecondEnd);
};
