#include "LiftAutoConnectConfig.h"

#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/WidgetExtension/CP_Float.h"
#include "Configuration/Properties/WidgetExtension/CP_Integer.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "ConveyorLiftAutoConnectModule.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "ConveyorLiftAutoConnect"

namespace
{
	const TCHAR* EnabledKey = TEXT("Enabled");
	const TCHAR* LiftTierKey = TEXT("LiftTier");
	const TCHAR* MaxLiftHeightKey = TEXT("MaxLiftHeight");
	const TCHAR* DetailedLoggingKey = TEXT("DetailedLogging");
	constexpr float MaxLiftHeightLimit = 1000.0f;
	const TCHAR* SMLPropertyPath = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/");

	FConfigId MakeConfigId()
	{
		FConfigId Id;
		Id.ModReference = TEXT("ConveyorLiftAutoConnect");
		return Id;
	}

	// Dropdown text of ELiftTierSource in builds without editor metadata.
	FText GetTierSourceDisplayName(int32 Index)
	{
		switch (static_cast<ELiftTierSource>(Index))
		{
		case ELiftTierSource::Blueprint:
			return LOCTEXT("TierBlueprint", "Blueprint side first");
		case ELiftTierSource::Faster:
			return LOCTEXT("TierFaster", "Faster lift");
		case ELiftTierSource::World:
			return LOCTEXT("TierWorld", "World side first");
		default:
			return FText::GetEmpty();
		}
	}

	template <typename TProperty>
	const TProperty* FindProperty(const UObject* WorldContext, const TCHAR* Key)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
		const UConfigPropertySection* Root = Manager ? Manager->GetConfigurationRootSection(MakeConfigId()) : nullptr;
		const TObjectPtr<UConfigProperty>* Found = Root ? Root->SectionProperties.Find(Key) : nullptr;
		return Found ? Cast<TProperty>(Found->Get()) : nullptr;
	}

	// SML's Blueprint subclass of a property class, e.g. BP_ConfigPropertyFloat for UConfigPropertyFloat.
	UClass* LoadEditorClass(const UClass* NativeClass)
	{
		const FString Name = TEXT("BP_") + NativeClass->GetName();
		const FString Path = FString::Printf(TEXT("%s%s.%s_C"), SMLPropertyPath, *Name, *Name);
		UClass* Loaded = LoadClass<UConfigProperty>(nullptr, *Path);
		if (!Loaded || !Loaded->IsChildOf(NativeClass))
		{
			UE_LOG(LogConveyorLiftAutoConnect, Warning, TEXT("SML editor class %s not found; that setting won't be editable in the Mods menu"), *Path);
			return nullptr;
		}
		return Loaded;
	}

	UConfigProperty* CloneAs(const UConfigProperty* Source, UClass* EditorClass, UObject* Outer)
	{
		UConfigProperty* Clone = NewObject<UConfigProperty>(Outer, EditorClass, NAME_None, RF_Public);
		for (TFieldIterator<FProperty> It(Source->GetClass()); It; ++It)
		{
			It->CopyCompleteValue_InContainer(Clone, Source);
		}
		return Clone;
	}

	// Widget and range of each number setting; SML defaults to a 0 to 1 text box.
	void ConfigureWidget(const FString& Key, UConfigProperty* Property)
	{
		if (UCP_Float* Float = Cast<UCP_Float>(Property); Float && Key == MaxLiftHeightKey)
		{
			Float->MinValue = 0.0f;
			Float->MaxValue = MaxLiftHeightLimit;
		}
		if (UCP_Integer* Integer = Cast<UCP_Integer>(Property); Integer && Key == LiftTierKey)
		{
			Integer->WidgetType = ECP_IntegerWidgetType::CPI_Enum;
			Integer->EnumClass = StaticEnum<ELiftTierSource>();
			Integer->MinValue = 0;
			Integer->MaxValue = static_cast<int32>(ELiftTierSource::World);
		}
	}
}

ULiftAutoConnectConfig::ULiftAutoConnectConfig()
{
	ConfigId = MakeConfigId();
	DisplayName = LOCTEXT("ConfigName", "Conveyor Lift Auto-Connect Plus");
	Description = LOCTEXT("ConfigDescription", "How generated lifts between stacked blueprints are chosen. These settings apply to the player placing the blueprint.");
	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));

	UConfigPropertyBool* Enabled = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("Enabled"));
	Enabled->DisplayName = LOCTEXT("Enabled", "Auto-connect lifts");
	Enabled->Tooltip = LOCTEXT("EnabledTip", "Connect aligned vertical conveyor endpoints when you place a blueprint. Turn off to place blueprints without generated lifts; nothing already built changes.");
	Enabled->DefaultValue = true;
	Enabled->Value = true;
	RootSection->SectionProperties.Add(EnabledKey, Enabled);

	UConfigPropertyInteger* LiftTier = CreateDefaultSubobject<UConfigPropertyInteger>(TEXT("LiftTier"));
	LiftTier->DisplayName = LOCTEXT("LiftTier", "Lift tier");
	LiftTier->Tooltip = LOCTEXT("LiftTierTip", "Which lift a generated lift copies its tier from. Blueprint side first: the lift next to the endpoint in the blueprint you are placing, else the one in the world. Faster lift: the faster of the two. World side first: the lift in the world, else the blueprint's. Without a lift on either side, no lift is generated.");
	LiftTier->DefaultValue = static_cast<int32>(ELiftTierSource::Blueprint);
	LiftTier->Value = LiftTier->DefaultValue;
	RootSection->SectionProperties.Add(LiftTierKey, LiftTier);

	UConfigPropertyFloat* MaxLiftHeight = CreateDefaultSubobject<UConfigPropertyFloat>(TEXT("MaxLiftHeight"));
	MaxLiftHeight->DisplayName = LOCTEXT("MaxLiftHeight", "Longest lift (m)");
	MaxLiftHeight->Tooltip = LOCTEXT("MaxLiftHeightTip", "Don't generate lifts longer than this, in meters (0 to 1000). 0 allows any length vanilla allows. The nearest endpoint below or above is still the only candidate, so a lift never skips past a closer floor.");
	MaxLiftHeight->DefaultValue = 0.0f;
	MaxLiftHeight->Value = 0.0f;
	RootSection->SectionProperties.Add(MaxLiftHeightKey, MaxLiftHeight);

	UConfigPropertyBool* DetailedLogging = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("DetailedLogging"));
	DetailedLogging->DisplayName = LOCTEXT("DetailedLogging", "Detailed log");
	DetailedLogging->Tooltip = LOCTEXT("DetailedLoggingTip", "Write why each endpoint did or didn't connect, and a check of every placed seam, to FactoryGame.log. Useful for bug reports; costs a little time per placement in large factories.");
	DetailedLogging->DefaultValue = false;
	DetailedLogging->Value = false;
	RootSection->SectionProperties.Add(DetailedLoggingKey, DetailedLogging);
}

FLiftAutoConnectSettings ULiftAutoConnectConfig::Read(const UObject* WorldContext)
{
	FLiftAutoConnectSettings Settings;
	if (const UConfigPropertyBool* Enabled = FindProperty<UConfigPropertyBool>(WorldContext, EnabledKey))
	{
		Settings.bEnabled = Enabled->Value;
	}
	if (const UConfigPropertyInteger* LiftTier = FindProperty<UConfigPropertyInteger>(WorldContext, LiftTierKey))
	{
		Settings.TierSource = static_cast<ELiftTierSource>(FMath::Clamp(LiftTier->Value, 0, static_cast<int32>(ELiftTierSource::World)));
	}
	if (const UConfigPropertyFloat* MaxLiftHeight = FindProperty<UConfigPropertyFloat>(WorldContext, MaxLiftHeightKey))
	{
		Settings.MaxLiftHeight = FMath::Clamp(MaxLiftHeight->Value, 0.0f, MaxLiftHeightLimit) * 100.0f;
	}
	if (const UConfigPropertyBool* DetailedLogging = FindProperty<UConfigPropertyBool>(WorldContext, DetailedLoggingKey))
	{
		Settings.bDetailedLogging = DetailedLogging->Value;
	}
	return Settings;
}

void ULiftAutoConnectConfig::ApplyLogging(const FLiftAutoConnectSettings& Settings)
{
	static const ELogVerbosity::Type StartupVerbosity = LogConveyorLiftAutoConnect.GetVerbosity();
	const ELogVerbosity::Type Wanted = Settings.bDetailedLogging
		? static_cast<ELogVerbosity::Type>(FMath::Max<int32>(StartupVerbosity, ELogVerbosity::Verbose))
		: StartupVerbosity;
	if (LogConveyorLiftAutoConnect.GetVerbosity() != Wanted)
	{
		LogConveyorLiftAutoConnect.SetVerbosity(Wanted);
	}
}

void ULiftAutoConnectConfig::UseSMLEditorClasses()
{
	StaticEnum<ELiftTierSource>()->SetEnumDisplayNameFn(&GetTierSourceDisplayName);

	ULiftAutoConnectConfig* Defaults = GetMutableDefault<ULiftAutoConnectConfig>();
	UConfigPropertySection* OldRoot = Defaults->RootSection;
	UClass* SectionClass = OldRoot ? LoadEditorClass(UConfigPropertySection::StaticClass()) : nullptr;
	if (!SectionClass || OldRoot->GetClass() == SectionClass)
	{
		return;
	}

	UConfigPropertySection* NewRoot = Cast<UConfigPropertySection>(CloneAs(OldRoot, SectionClass, Defaults));
	NewRoot->SectionProperties.Reset();
	if (UCP_Section* EditorSection = Cast<UCP_Section>(NewRoot))
	{
		EditorSection->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
		EditorSection->HasHeader = false;
	}
	for (const TPair<FString, TObjectPtr<UConfigProperty>>& Pair : OldRoot->SectionProperties)
	{
		UConfigProperty* Old = Pair.Value;
		UClass* EditorClass = Old ? LoadEditorClass(Old->GetClass()) : nullptr;
		if (!EditorClass)
		{
			NewRoot->SectionProperties.Add(Pair.Key, Old);
			continue;
		}
		UConfigProperty* Clone = CloneAs(Old, EditorClass, NewRoot);
		ConfigureWidget(Pair.Key, Clone);
		NewRoot->SectionProperties.Add(Pair.Key, Clone);
	}
	Defaults->RootSection = NewRoot;
}

ULiftAutoConnectGameInstanceModule::ULiftAutoConnectGameInstanceModule()
{
	bRootModule = true;
	ModConfigurations.Add(ULiftAutoConnectConfig::StaticClass());
}

void ULiftAutoConnectGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	// SML registers the configuration during initialization; the editor classes must be in place first.
	if (Phase == ELifecyclePhase::INITIALIZATION)
	{
		ULiftAutoConnectConfig::UseSMLEditorClasses();
	}
	Super::DispatchLifecycleEvent(Phase);
}

#undef LOCTEXT_NAMESPACE
