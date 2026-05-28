// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreGamePhaseSubsystem.h"
#include "Dark_TdoreGamePhaseAbility.h"
#include "Dark_TdoreGamePhaseLog.h"
#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreGamePhaseSubsystem)

DEFINE_LOG_CATEGORY(LogDarkTdoreGamePhase);

// ============ USubsystem 生命周期 ============

bool UDark_TdoreGamePhaseSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && true;
}

bool UDark_TdoreGamePhaseSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

// ============ StartPhase — 启动一个游戏阶段 ============
// 原理：把 PhaseAbility 作为一次性 GA 授予 GameState 的 ASC 并激活
//       激活成功 → 注册到 ActivePhaseMap → 等 PhaseAbility 结束时回调

void UDark_TdoreGamePhaseSubsystem::StartPhase(TSubclassOf<UDark_TdoreGamePhaseAbility> PhaseAbility,
	FDarkTdoreGamePhaseDelegate PhaseEndedCallback)
{
	UWorld* World = GetWorld();
	UDark_TdoreAbilitySystemComponent* GameState_ASC =
		World->GetGameState()->FindComponentByClass<UDark_TdoreAbilitySystemComponent>();

	if (ensure(GameState_ASC))
	{
		// Step 1: 创建 AbilitySpec → GiveAbilityAndActivateOnce 授予并立即激活
		FGameplayAbilitySpec PhaseSpec(PhaseAbility, 1, 0, this);
		FGameplayAbilitySpecHandle SpecHandle = GameState_ASC->GiveAbilityAndActivateOnce(PhaseSpec);
		FGameplayAbilitySpec* FoundSpec = GameState_ASC->FindAbilitySpecFromHandle(SpecHandle);

		if (FoundSpec && FoundSpec->IsActive())
		{
			// Step 2: 记录到活跃阶段表
			FGamePhaseEntry& Entry = ActivePhaseMap.FindOrAdd(SpecHandle);
			Entry.PhaseEndedCallback = PhaseEndedCallback;
		}
		else
		{
			// 激活失败（如被 BlockedTag 阻止）
			PhaseEndedCallback.ExecuteIfBound(nullptr);
		}
	}
}

void UDark_TdoreGamePhaseSubsystem::K2_StartPhase(TSubclassOf<UDark_TdoreGamePhaseAbility> PhaseAbility,
	const FDarkTdoreGamePhaseDynamicDelegate& PhaseEndedDelegate)
{
	const FDarkTdoreGamePhaseDelegate EndedDelegate =
		FDarkTdoreGamePhaseDelegate::CreateWeakLambda(
			const_cast<UObject*>(PhaseEndedDelegate.GetUObject()),
			[PhaseEndedDelegate](const UDark_TdoreGamePhaseAbility* PhaseAbility) {
				PhaseEndedDelegate.ExecuteIfBound(PhaseAbility);
			});

	StartPhase(PhaseAbility, EndedDelegate);
}

// ============ 观察者：监听阶段启动/结束 ============

void UDark_TdoreGamePhaseSubsystem::WhenPhaseStartsOrIsActive(FGameplayTag PhaseTag,
	EPhaseTagMatchType MatchType, const FDarkTdoreGamePhaseTagDelegate& WhenPhaseActive)
{
	FPhaseObserver Observer;
	Observer.PhaseTag = PhaseTag;
	Observer.MatchType = MatchType;
	Observer.PhaseCallback = WhenPhaseActive;
	PhaseStartObservers.Add(Observer);

	// 如果阶段已在活跃中，立即回调一次
	if (IsPhaseActive(PhaseTag))
	{
		WhenPhaseActive.ExecuteIfBound(PhaseTag);
	}
}

void UDark_TdoreGamePhaseSubsystem::WhenPhaseEnds(FGameplayTag PhaseTag,
	EPhaseTagMatchType MatchType, const FDarkTdoreGamePhaseTagDelegate& WhenPhaseEnd)
{
	FPhaseObserver Observer;
	Observer.PhaseTag = PhaseTag;
	Observer.MatchType = MatchType;
	Observer.PhaseCallback = WhenPhaseEnd;
	PhaseEndObservers.Add(Observer);
}

void UDark_TdoreGamePhaseSubsystem::K2_WhenPhaseStartsOrIsActive(FGameplayTag PhaseTag,
	EPhaseTagMatchType MatchType, FDarkTdoreGamePhaseTagDynamicDelegate WhenPhaseActive)
{
	const FDarkTdoreGamePhaseTagDelegate ActiveDelegate =
		FDarkTdoreGamePhaseTagDelegate::CreateWeakLambda(WhenPhaseActive.GetUObject(),
			[WhenPhaseActive](const FGameplayTag& PhaseTag) {
				WhenPhaseActive.ExecuteIfBound(PhaseTag);
			});
	WhenPhaseStartsOrIsActive(PhaseTag, MatchType, ActiveDelegate);
}

void UDark_TdoreGamePhaseSubsystem::K2_WhenPhaseEnds(FGameplayTag PhaseTag,
	EPhaseTagMatchType MatchType, FDarkTdoreGamePhaseTagDynamicDelegate WhenPhaseEnd)
{
	const FDarkTdoreGamePhaseTagDelegate EndedDelegate =
		FDarkTdoreGamePhaseTagDelegate::CreateWeakLambda(WhenPhaseEnd.GetUObject(),
			[WhenPhaseEnd](const FGameplayTag& PhaseTag) {
				WhenPhaseEnd.ExecuteIfBound(PhaseTag);
			});
	WhenPhaseEnds(PhaseTag, MatchType, EndedDelegate);
}

// ============ IsPhaseActive — 查询阶段是否活跃 ============

bool UDark_TdoreGamePhaseSubsystem::IsPhaseActive(const FGameplayTag& PhaseTag) const
{
	for (const auto& KVP : ActivePhaseMap)
	{
		if (KVP.Value.PhaseTag.MatchesTag(PhaseTag))
		{
			return true;
		}
	}
	return false;
}

// ============ OnBeginPhase — 新阶段激活时的内部处理 ============
// 核心逻辑：
//   1. 获取新阶段的 PhaseTag
//   2. 遍历所有活跃阶段 → 如果新 Tag 不匹配活跃 Tag（即不是祖先关系）→ 取消它
//   3. 记录新阶段到 ActivePhaseMap
//   4. 通知所有 PhaseStartObservers

void UDark_TdoreGamePhaseSubsystem::OnBeginPhase(
	const UDark_TdoreGamePhaseAbility* PhaseAbility, const FGameplayAbilitySpecHandle Handle)
{
	const FGameplayTag IncomingTag = PhaseAbility->GetGamePhaseTag();
	UE_LOG(LogDarkTdoreGamePhase, Log, TEXT("[GamePhase] 开始阶段: '%s' (%s)"),
		*IncomingTag.ToString(), *GetNameSafe(PhaseAbility));

	UWorld* World = GetWorld();
	UDark_TdoreAbilitySystemComponent* GameState_ASC =
		World->GetGameState()->FindComponentByClass<UDark_TdoreAbilitySystemComponent>();

	if (ensure(GameState_ASC))
	{
		// Step 1: 收集所有需要取消的活跃阶段
		TArray<FGameplayAbilitySpecHandle> ToCancel;
		for (const auto& KVP : ActivePhaseMap)
		{
			const FGameplayAbilitySpec* Spec = GameState_ASC->FindAbilitySpecFromHandle(KVP.Key);
			if (!Spec) continue;

			const UDark_TdoreGamePhaseAbility* ActiveAbility =
				CastChecked<UDark_TdoreGamePhaseAbility>(Spec->Ability);
			const FGameplayTag ActiveTag = ActiveAbility->GetGamePhaseTag();

			// 如果新阶段 Tag 不是活跃阶段 Tag 的祖先 → 取消
			// 例：新=Game.Playing.SuddenDeath, 活跃=Game.Playing.WarmUp
			//     IncomingTag.MatchesTag(ActiveTag)? SuddenDeath.Matches(WarmUp)? No → 取消
			if (!IncomingTag.MatchesTag(ActiveTag))
			{
				UE_LOG(LogDarkTdoreGamePhase, Log, TEXT("  → 结束冲突阶段: '%s' (%s)"),
					*ActiveTag.ToString(), *GetNameSafe(ActiveAbility));
				ToCancel.Add(KVP.Key);
			}
		}

		// Step 2: 取消冲突阶段
		for (const FGameplayAbilitySpecHandle& H : ToCancel)
		{
			GameState_ASC->CancelAbilityHandle(H);
		}

		// Step 3: 记录新阶段
		FGamePhaseEntry& Entry = ActivePhaseMap.FindOrAdd(Handle);
		Entry.PhaseTag = IncomingTag;

		// Step 4: 通知启动观察者
		for (const FPhaseObserver& Observer : PhaseStartObservers)
		{
			if (Observer.IsMatch(IncomingTag))
			{
				Observer.PhaseCallback.ExecuteIfBound(IncomingTag);
			}
		}
	}
}

// ============ OnEndPhase — 阶段结束时的内部处理 ============

void UDark_TdoreGamePhaseSubsystem::OnEndPhase(
	const UDark_TdoreGamePhaseAbility* PhaseAbility, const FGameplayAbilitySpecHandle Handle)
{
	const FGameplayTag EndedTag = PhaseAbility->GetGamePhaseTag();
	UE_LOG(LogDarkTdoreGamePhase, Log, TEXT("[GamePhase] 结束阶段: '%s' (%s)"),
		*EndedTag.ToString(), *GetNameSafe(PhaseAbility));

	// 执行结束回调
	const FGamePhaseEntry& Entry = ActivePhaseMap.FindChecked(Handle);
	Entry.PhaseEndedCallback.ExecuteIfBound(PhaseAbility);

	// 从活跃表移除
	ActivePhaseMap.Remove(Handle);

	// 通知结束观察者
	for (const FPhaseObserver& Observer : PhaseEndObservers)
	{
		if (Observer.IsMatch(EndedTag))
		{
			Observer.PhaseCallback.ExecuteIfBound(EndedTag);
		}
	}
}

// ============ FPhaseObserver::IsMatch ============

bool UDark_TdoreGamePhaseSubsystem::FPhaseObserver::IsMatch(const FGameplayTag& ComparePhaseTag) const
{
	switch (MatchType)
	{
	case EPhaseTagMatchType::ExactMatch:
		return ComparePhaseTag == PhaseTag;
	case EPhaseTagMatchType::PartialMatch:
		return ComparePhaseTag.MatchesTag(PhaseTag);
	}
	return false;
}
