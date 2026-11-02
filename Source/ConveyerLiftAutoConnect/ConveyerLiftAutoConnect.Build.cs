using UnrealBuildTool;

public class ConveyerLiftAutoConnect : ModuleRules
{
	public ConveyerLiftAutoConnect(ReadOnlyTargetRules Target) : base(Target)
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
