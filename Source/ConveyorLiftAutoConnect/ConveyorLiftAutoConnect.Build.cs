using UnrealBuildTool;

public class ConveyorLiftAutoConnect : ModuleRules
{
	public ConveyorLiftAutoConnect(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Projects",
			"FactoryGame",
			"SML",
			"DummyHeaders"
		});
	}
}
