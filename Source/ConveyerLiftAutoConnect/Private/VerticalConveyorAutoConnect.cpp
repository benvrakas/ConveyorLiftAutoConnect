#include "VerticalConveyorAutoConnect.h"

#include "BlueprintVerticalConveyorConnectionManager.h"
#include "FGConstructDisqualifier.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Interfaces/IPluginManager.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY(LogVerticalConveyorAutoConnect);

void FVerticalConveyorAutoConnectModule::
	HandleConveyorLiftValidationAfter(AFGConveyorLiftHologram* hologram)
{
	if (!IsValid(hologram))
	{
		return;
	}

	const bool isAutoBridge =
		hologram->ActorHasTag(
			VerticalConveyorAutoConnect::
				PreviewInvalidFloorSuppressionTag()) &&
		IsValid(Cast<AFGBlueprintHologram>(hologram->GetParentHologram()));

	// Preserve the one narrow compatibility correction we have actually proven:
	// our synthetic child can transiently inherit InvalidFloor from the blueprint
	// placement context even though a completed lift does not require a floor.
	if (isAutoBridge)
	{
		TArray<TSubclassOf<UFGConstructDisqualifier>> disqualifiers;
		hologram->GetConstructDisqualifiers(disqualifiers);
		const int32 removedCount = disqualifiers.RemoveAll(
			[](const TSubclassOf<UFGConstructDisqualifier>& disqualifier)
			{
				return disqualifier.Get() == UFGCDInvalidFloor::StaticClass();
			});

		if (removedCount > 0)
		{
			hologram->ResetConstructDisqualifiers();
			for (const TSubclassOf<UFGConstructDisqualifier>& disqualifier :
				 disqualifiers)
			{
				hologram->AddConstructDisqualifier(disqualifier);
			}
		}
	}
}

void FVerticalConveyorAutoConnectModule::StartupModule()
{
	if (WITH_EDITOR)
	{
		return;
	}

	// Two copies of the manager would each generate a bridge for every seam.
	const TSharedPtr<IPlugin> originalPlugin =
		IPluginManager::Get().FindPlugin(TEXT("VerticalConveyorAutoConnect"));
	if (originalPlugin.IsValid() && originalPlugin->IsEnabled())
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Error,
			TEXT("ConveyerLiftAutoConnect: VerticalConveyorAutoConnect is also installed; this fork stays inactive. Remove one of the two mods."));
		return;
	}

	BlueprintBeginPlayHook = SUBSCRIBE_METHOD_VIRTUAL(
		AFGBlueprintHologram::BeginPlay,
		GetMutableDefault<AFGBlueprintHologram>(),
		[](auto& /*scope*/, AFGBlueprintHologram* hologram)
		{
			if (IsValid(hologram))
			{
				hologram->RegisterOpenConnectionManager<
					FBlueprintVerticalConveyorConnectionManager>();
				UE_LOG(
					LogVerticalConveyorAutoConnect,
					Verbose,
					TEXT("VerticalConveyorAutoConnect: registered manager on %s"),
					*hologram->GetName());
			}
		});

	ConveyorLiftValidationHook = SUBSCRIBE_METHOD_VIRTUAL_AFTER(
		AFGConveyorLiftHologram::CheckValidPlacement,
		GetMutableDefault<AFGConveyorLiftHologram>(),
		[](AFGConveyorLiftHologram* hologram)
		{
			HandleConveyorLiftValidationAfter(hologram);
		});
}

void FVerticalConveyorAutoConnectModule::ShutdownModule()
{
	if (BlueprintBeginPlayHook.IsValid())
	{
		UNSUBSCRIBE_METHOD(AFGBlueprintHologram::BeginPlay, BlueprintBeginPlayHook);
		BlueprintBeginPlayHook.Reset();
	}

	if (ConveyorLiftValidationHook.IsValid())
	{
		UNSUBSCRIBE_METHOD(
			AFGConveyorLiftHologram::CheckValidPlacement,
			ConveyorLiftValidationHook);
		ConveyorLiftValidationHook.Reset();
	}
}

IMPLEMENT_MODULE(
	FVerticalConveyorAutoConnectModule,
	ConveyerLiftAutoConnect)
