#include "ConveyorLiftAutoConnectModule.h"

#include "FGConstructDisqualifier.h"
#include "FGConstructionMessageInterface.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGRecipe.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Interfaces/IPluginManager.h"
#include "LiftAutoConnectManager.h"
#include "LiftBridgeHologram.h"
#include "Patching/NativeHookManager.h"

DEFINE_LOG_CATEGORY(LogConveyorLiftAutoConnect);

namespace
{
	// Spelled without a comma so it can appear inside a hook macro argument.
	using FChildHologramMap = TMap<FName, TObjectPtr<AFGHologram>>;
}

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

	// Leaves bridge children out of the construct message; the receiver rebuilds them from the manager's seam bits.
	BlueprintSerializeConstructMessageHook = SUBSCRIBE_METHOD_VIRTUAL(AFGBlueprintHologram::SerializeConstructMessage,
		GetMutableDefault<AFGBlueprintHologram>(),
		[](auto& Scope, AFGBlueprintHologram* InterfaceThis, FArchive& Archive, FNetConstructionID Id)
		{
			// The hooked body overrides an IFGConstructionMessageInterface function, so its this is that base
			// sub-object; Scope takes it unchanged.
			AFGBlueprintHologram* Hologram =
				static_cast<AFGBlueprintHologram*>(reinterpret_cast<IFGConstructionMessageInterface*>(InterfaceThis));
			if (!IsValid(Hologram) || !Archive.IsSaving())
			{
				return;
			}
			auto IsBridgeName = [](const FName& Name)
			{
				return Name.ToString().StartsWith(FLiftBridgeHologram::ChildNamePrefix, ESearchCase::CaseSensitive);
			};

			const TArray<TObjectPtr<AFGHologram>> Children = Hologram->mChildren;
			const FChildHologramMap ChildrenByName = Hologram->mChildrenNameLookupMap;
			int32 Hidden = 0;
			for (auto It = Hologram->mChildrenNameLookupMap.CreateIterator(); It; ++It)
			{
				if (IsBridgeName(It.Key()))
				{
					Hologram->mChildren.Remove(It.Value());
					It.RemoveCurrent();
					++Hidden;
				}
			}
			Scope(InterfaceThis, Archive, Id);
			if (Hidden > 0)
			{
				Hologram->mChildren = Children;
				Hologram->mChildrenNameLookupMap = ChildrenByName;
			}
		});

	// Logs a placement request over the engine's 65535 byte replicated array limit, which the game drops silently.
	ConstructHologramRequestHook = SUBSCRIBE_METHOD(UFGBuildGunStateBuild::Server_ConstructHologram,
		[](auto& /*Scope*/, UFGBuildGunStateBuild* /*State*/, FNetConstructionID /*Id*/, FConstructHologramMessage Message)
		{
			constexpr int32 MaxBytes = 65534;
			if (Message.SerializedHologramData.Num() > MaxBytes)
			{
				UE_LOG(LogConveyorLiftAutoConnect, Error,
					TEXT("Placement request for %s is %d bytes (%lld bits), over the %d byte limit; the game will drop it"),
					*GetNameSafe(Message.Recipe.Get()), Message.SerializedHologramData.Num(), Message.NumBits, MaxBytes);
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
	if (BlueprintSerializeConstructMessageHook.IsValid())
	{
		UNSUBSCRIBE_METHOD(AFGBlueprintHologram::SerializeConstructMessage, BlueprintSerializeConstructMessageHook);
		BlueprintSerializeConstructMessageHook.Reset();
	}
	if (ConstructHologramRequestHook.IsValid())
	{
		UNSUBSCRIBE_METHOD(UFGBuildGunStateBuild::Server_ConstructHologram, ConstructHologramRequestHook);
		ConstructHologramRequestHook.Reset();
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
