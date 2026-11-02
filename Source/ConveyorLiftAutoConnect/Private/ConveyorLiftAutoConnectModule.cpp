#include "ConveyorLiftAutoConnectModule.h"

#include "FGConstructDisqualifier.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Interfaces/IPluginManager.h"
#include "LiftAutoConnectManager.h"
#include "LiftBridgeHologram.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY(LogConveyorLiftAutoConnect);

void FConveyorLiftAutoConnectModule::StartupModule()
{
	if (WITH_EDITOR)
	{
		return;
	}

	// Two managers would each build a bridge for every seam.
	const TSharedPtr<IPlugin> OriginalMod = IPluginManager::Get().FindPlugin(TEXT("VerticalConveyorAutoConnect"));
	if (OriginalMod.IsValid() && OriginalMod->IsEnabled())
	{
		UE_LOG(LogConveyorLiftAutoConnect, Error,
			TEXT("VerticalConveyorAutoConnect is also installed, so Conveyor Lift Auto-Connect Plus stays inactive. Remove one of the two mods."));
		return;
	}

	BlueprintBeginPlayHook = SUBSCRIBE_METHOD_VIRTUAL(AFGBlueprintHologram::BeginPlay, GetMutableDefault<AFGBlueprintHologram>(),
		[](auto& /*Scope*/, AFGBlueprintHologram* Hologram)
		{
			if (IsValid(Hologram))
			{
				Hologram->RegisterOpenConnectionManager<FLiftAutoConnectManager>();
			}
		});

	LiftCheckValidPlacementHook = SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGConveyorLiftHologram::CheckValidPlacement,
		GetMutableDefault<AFGConveyorLiftHologram>(),
		[](AFGConveyorLiftHologram* Hologram)
		{
			RemoveTransientInvalidFloor(Hologram);
		});
}

void FConveyorLiftAutoConnectModule::ShutdownModule()
{
	if (BlueprintBeginPlayHook.IsValid())
	{
		UNSUBSCRIBE_METHOD(AFGBlueprintHologram::BeginPlay, BlueprintBeginPlayHook);
		BlueprintBeginPlayHook.Reset();
	}
	if (LiftCheckValidPlacementHook.IsValid())
	{
		UNSUBSCRIBE_METHOD(AFGConveyorLiftHologram::CheckValidPlacement, LiftCheckValidPlacementHook);
		LiftCheckValidPlacementHook.Reset();
	}
}

void FConveyorLiftAutoConnectModule::RemoveTransientInvalidFloor(AFGConveyorLiftHologram* Hologram)
{
	if (!IsValid(Hologram) ||
		!Hologram->ActorHasTag(FLiftBridgeHologram::SyntheticBridgeTag()) ||
		!IsValid(Cast<AFGBlueprintHologram>(Hologram->GetParentHologram())))
	{
		return;
	}

	TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
	Hologram->GetConstructDisqualifiers(Disqualifiers);
	const int32 RemovedCount = Disqualifiers.RemoveAll([](const TSubclassOf<UFGConstructDisqualifier>& Disqualifier)
	{
		return Disqualifier.Get() == UFGCDInvalidFloor::StaticClass();
	});
	if (RemovedCount == 0)
	{
		return;
	}

	Hologram->ResetConstructDisqualifiers();
	for (const TSubclassOf<UFGConstructDisqualifier>& Disqualifier : Disqualifiers)
	{
		Hologram->AddConstructDisqualifier(Disqualifier);
	}
}

IMPLEMENT_MODULE(FConveyorLiftAutoConnectModule, ConveyorLiftAutoConnect)
