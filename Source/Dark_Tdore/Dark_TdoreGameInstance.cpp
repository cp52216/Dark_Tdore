// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGameInstance.h"

#include "Components/GameFrameworkComponentManager.h"
#include "GameplayTagContainer.h"

// 参考 Lyra ULyraGameInstance::Init()
// 向 GameFrameworkComponentManager 注册 InitState 状态链：
//   Spawned → DataAvailable → DataInitialized → GameplayReady
//
// 每个状态指定其依赖的前置状态（PreviousState），形成 DAG 依赖图。
// 组件的 CanChangeInitState 检查前置状态是否满足 + 自定义条件是否达成。
// GameFrameworkComponentManager 自动按拓扑序推进所有组件的状态。
void UDark_TdoreGameInstance::Init()
{
	Super::Init();

	UGameFrameworkComponentManager* ComponentManager = GetSubsystem<UGameFrameworkComponentManager>();
	if (ensure(ComponentManager))
	{
		// InitState.Spawned — 出生状态，无前置，所有组件 BeginPlay 后到达
		ComponentManager->RegisterInitState(
			FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned")), false, FGameplayTag());

		// InitState.DataAvailable — 数据就绪，依赖 Spawned
		// PawnExtComponent 检查：有 Controller?（Authority/Local）→ 到达
		ComponentManager->RegisterInitState(
			FGameplayTag::RequestGameplayTag(TEXT("InitState.DataAvailable")), false,
			FGameplayTag::RequestGameplayTag(TEXT("InitState.Spawned")));

		// InitState.DataInitialized — 数据初始化完成，依赖 DataAvailable
		// PawnExtComponent 检查：所有特征都到达 DataAvailable? → 到达
		ComponentManager->RegisterInitState(
			FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized")), false,
			FGameplayTag::RequestGameplayTag(TEXT("InitState.DataAvailable")));

		// InitState.GameplayReady — 完全就绪，依赖 DataInitialized
		// 到达此状态后角色可开始移动/攻击等完整玩法
		ComponentManager->RegisterInitState(
			FGameplayTag::RequestGameplayTag(TEXT("InitState.GameplayReady")), false,
			FGameplayTag::RequestGameplayTag(TEXT("InitState.DataInitialized")));
	}
}
