#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "Module/GameInstanceModule.h"
#include "LiftAutoConnectConfig.generated.h"

// Whose adjacent lift sets the tier of a generated lift.
UENUM()
enum class ELiftTierSource : uint8
{
	Blueprint UMETA(DisplayName = "Blueprint side first"),
	Faster UMETA(DisplayName = "Faster lift"),
	World UMETA(DisplayName = "World side first")
};

struct FLiftAutoConnectSettings
{
	bool bEnabled = true;
	ELiftTierSource TierSource = ELiftTierSource::Blueprint;
	// Longest generated lift in cm; 0 leaves the length to vanilla.
	float MaxLiftHeight = 0.0f;
	bool bDetailedLogging = false;
};

// The mod's page in the Mods menu and in Satisfactory Mod Manager.
UCLASS()
class CONVEYORLIFTAUTOCONNECT_API ULiftAutoConnectConfig : public UModConfiguration
{
	GENERATED_BODY()

public:
	ULiftAutoConnectConfig();

	// The current settings, or the defaults when the configuration isn't registered.
	static FLiftAutoConnectSettings Read(const UObject* WorldContext);
	// Raises the log category to Verbose while detailed logging is on; otherwise keeps its startup verbosity.
	static void ApplyLogging(const FLiftAutoConnectSettings& Settings);
	// Rebuilds the default configuration with SML's Blueprint property classes, which carry the Mods menu widgets.
	// Call before SML registers the configuration.
	static void UseSMLEditorClasses();
};

// Root game instance module; registers the configuration with SML.
UCLASS()
class CONVEYORLIFTAUTOCONNECT_API ULiftAutoConnectGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	ULiftAutoConnectGameInstanceModule();

	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};
