// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Dark_Tdore : ModuleRules
{
	public Dark_Tdore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"GameplayAbilities",
			"GameplayMessageRuntime",
			"GameplayTags",
			"GameplayTasks",
			"ModularGameplay",
			"ModularGameplayActors",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Dark_Tdore",
			"Dark_Tdore/AbilitySystem",
			"Dark_Tdore/AbilitySystem/Abilities",
			"Dark_Tdore/AbilitySystem/Attributes",
			"Dark_Tdore/AbilitySystem/Executions",
			"Dark_Tdore/Messages"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true

		// Iris 网络序列化：FDark_TdoreGameplayEffectContext 需要
		// UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER 宏依赖 IrisCore 模块
		SetupIrisSupport(Target);
	}
}
