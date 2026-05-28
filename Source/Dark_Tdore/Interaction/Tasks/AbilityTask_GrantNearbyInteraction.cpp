// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilityTask_GrantNearbyInteraction.h"
#include "AbilitySystemComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Interaction/Dark_TdoreInteractableTarget.h"
#include "Interaction/Dark_TdoreInteractionOption.h"
#include "Interaction/Dark_TdoreInteractionQuery.h"
#include "Interaction/Dark_TdoreInteractionStatics.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbilityTask_GrantNearbyInteraction)

// ============ 构造 ============

UAbilityTask_GrantNearbyInteraction::UAbilityTask_GrantNearbyInteraction(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer) {}

// ============ GrantAbilitiesForNearbyInteractors — 静态工厂 ============
// 由 Interact GA::ActivateAbility 调用，创建 Task 并设置参数

UAbilityTask_GrantNearbyInteraction* UAbilityTask_GrantNearbyInteraction::GrantAbilitiesForNearbyInteractors(
	UGameplayAbility* OwningAbility, float InteractionScanRange, float InteractionScanRate)
{
	UAbilityTask_GrantNearbyInteraction* MyObj = NewAbilityTask<UAbilityTask_GrantNearbyInteraction>(OwningAbility);
	MyObj->InteractionScanRange = InteractionScanRange;
	MyObj->InteractionScanRate = InteractionScanRate;
	return MyObj;
	// 调用方需手动 MyObj->ReadyForActivation() 触发 Activate()
}

// ============ Activate — 启动扫描 Timer ============
// 通过 WorldTimerManager 每 InteractionScanRate 秒触发一次 QueryInteractables
// SetWaitingOnAvatar() 确保 Avatar 有效前不执行

void UAbilityTask_GrantNearbyInteraction::Activate()
{
	SetWaitingOnAvatar();

	UWorld* World = GetWorld();
	World->GetTimerManager().SetTimer(
		QueryTimerHandle,                                // 输出句柄
		this,                                             // 回调所有者
		&ThisClass::QueryInteractables,                  // 回调成员函数
		InteractionScanRate,                             // 间隔时间
		true);                                           // 循环触发
}

// ============ OnDestroy — 清理 Timer ============

void UAbilityTask_GrantNearbyInteraction::OnDestroy(bool AbilityEnded)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueryTimerHandle);
	}
	Super::OnDestroy(AbilityEnded);
}

// ============ QueryInteractables — 核心扫描函数 ============
// 此函数每 InteractionScanRate 秒被 Timer 触发一次（仅在服务器执行）
//
// 完整流程：
//   1. OverlapMultiByChannel — 球形扫描附近 500cm 内的物理体
//   2. AppendInteractableTargetsFromOverlapResults — 过滤出 IInteractableTarget
//   3. GatherInteractionOptions — 收集交互选项
//   4. GiveAbility — 授予交互 GA（避免重复）
//
// 如果任何一步没有结果，直接 return（性能优化）

void UAbilityTask_GrantNearbyInteraction::QueryInteractables()
{
	UWorld* World = GetWorld();
	AActor* ActorOwner = GetAvatarActor();

	// 安全检查：World 或 Avatar 不存在 → 跳过
	if (!World || !ActorOwner) return;

	// ===== Step 1: 配置碰撞查询参数 =====
	FCollisionQueryParams Params(SCENE_QUERY_STAT(UAbilityTask_GrantNearbyInteraction), false);

	// ===== Step 2: 球形 Overlap 扫描 =====
	// 以角色当前位置为球心、InteractionScanRange 为半径、ECC_WorldDynamic 通道
	// 需要被检测到的 Actor 必须设置 ECC_WorldDynamic 碰撞响应
	TArray<FOverlapResult> OverlapResults;
	World->OverlapMultiByChannel(
		OUT OverlapResults,
		ActorOwner->GetActorLocation(),
		FQuat::Identity,
		ECC_WorldDynamic,
		FCollisionShape::MakeSphere(InteractionScanRange),
		Params);

	// 没有任何重叠 → 跳过后续逻辑
	if (OverlapResults.Num() == 0) return;

	// ===== Step 3: 从 Overlap 结果中提取实现了 IInteractableTarget 的目标 =====
	// Actor 自身或 Actor 上的 Component 都可能是交互目标
	TArray<TScriptInterface<IDark_TdoreInteractableTarget>> InteractableTargets;
	UDark_TdoreInteractionStatics::AppendInteractableTargetsFromOverlapResults(
		OverlapResults, OUT InteractableTargets);

	if (InteractableTargets.Num() > 0)
	{
		UE_LOG(LogDark_TdoreGAS, Verbose, TEXT("[Interaction] 扫描到 %d 个交互目标 (范围=%.0fcm)"),
			InteractableTargets.Num(), InteractionScanRange);
	}
	else
	{
		return; // 没有实现 IInteractableTarget 的目标 → 跳过
	}

	// ===== Step 4: 构建交互查询上下文 =====
	FDark_TdoreInteractionQuery InteractionQuery;
	InteractionQuery.RequestingAvatar = ActorOwner;
	InteractionQuery.RequestingController = Cast<AController>(ActorOwner->GetOwner());

	// ===== Step 5: 遍历每个交互目标，收集交互选项 =====
	// FInteractionOptionBuilder 自动将 Option.InteractableTarget 设置为当前 Scope
	TArray<FDark_TdoreInteractionOption> Options;
	for (TScriptInterface<IDark_TdoreInteractableTarget>& InteractiveTarget : InteractableTargets)
	{
		FInteractionOptionBuilder InteractionBuilder(InteractiveTarget, Options);
		InteractiveTarget->GatherInteractionOptions(InteractionQuery, InteractionBuilder);
	}

	// ===== Step 6: 授予交互 GA 给 ASC =====
	// 使用 InteractionAbilityCache 避免对同一个 GA 类重复 GiveAbility
	// 引擎不允许对 ASC 重复授予同一个 GA 类
	for (FDark_TdoreInteractionOption& Option : Options)
	{
		if (Option.InteractionAbilityToGrant)
		{
			FObjectKey ObjectKey(Option.InteractionAbilityToGrant);

			// 如果缓存中没有 → 尚未授予 → 授予并记录
			if (!InteractionAbilityCache.Find(ObjectKey))
			{
				FGameplayAbilitySpec Spec(Option.InteractionAbilityToGrant, 1, INDEX_NONE, this);
				FGameplayAbilitySpecHandle Handle = AbilitySystemComponent->GiveAbility(Spec);
				InteractionAbilityCache.Add(ObjectKey, Handle);

				UE_LOG(LogDark_TdoreGAS, Display, TEXT("[Interaction] 授予交互 GA: %s → %s"),
					*GetNameSafe(Option.InteractionAbilityToGrant),
					*GetNameSafe(AbilitySystemComponent->GetOwner()));
			}
		}
	}
}
