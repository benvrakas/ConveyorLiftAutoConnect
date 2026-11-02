#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class AFGConveyorLiftHologram;

DECLARE_LOG_CATEGORY_EXTERN(LogConveyorLiftAutoConnect, Log, All);

class FConveyorLiftAutoConnectModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	// Drops UFGCDInvalidFloor from a generated bridge preview; the finished lift needs no floor.
	static void RemoveTransientInvalidFloor(AFGConveyorLiftHologram* Hologram);

	FDelegateHandle BlueprintBeginPlayHook;
	FDelegateHandle LiftCheckValidPlacementHook;
};
