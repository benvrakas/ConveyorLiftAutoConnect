#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class AFGConveyorLiftHologram;

DECLARE_LOG_CATEGORY_EXTERN(LogVerticalConveyorAutoConnect, Log, All);

namespace VerticalConveyorAutoConnect
{
	inline const FName& PreviewInvalidFloorSuppressionTag()
	{
		static const FName Tag(
			TEXT("VerticalConveyorAutoConnect.PreviewInvalidFloorSuppression"));
		return Tag;
	}
}

class FVerticalConveyorAutoConnectModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	static void HandleConveyorLiftValidationAfter(
		AFGConveyorLiftHologram* hologram);

	FDelegateHandle BlueprintBeginPlayHook;
	FDelegateHandle ConveyorLiftValidationHook;
};
