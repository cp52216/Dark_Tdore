// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreHealthComponent.h"

#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/Dark_TdoreHealthSet.h"
#include "AbilitySystem/Dark_TdoreLogChannels.h"
#include "GameFramework/GameplayMessageSubsystem.h"
#include "Messages/Dark_TdoreVerbMessage.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreHealthComponent)

UDark_TdoreHealthComponent::UDark_TdoreHealthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);

	AbilitySystemComponent = nullptr;
	HealthSet = nullptr;
	DeathState = EDeathState::NotDead;
}

void UDark_TdoreHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UDark_TdoreHealthComponent, DeathState);
}

void UDark_TdoreHealthComponent::OnUnregister()
{
	UninitializeFromAbilitySystem();
	Super::OnUnregister();
}

// 参考 Lyra ULyraHealthComponent::InitializeWithAbilitySystem
void UDark_TdoreHealthComponent::InitializeWithAbilitySystem(UDark_TdoreAbilitySystemComponent* InASC)
{
	AActor* Owner = GetOwner();
	check(Owner);

	if (AbilitySystemComponent)
	{
		UE_LOG(LogDark_TdoreGAS, Error, TEXT("HealthComponent: [%s] 已初始化，跳过"), *GetNameSafe(Owner));
		return;
	}

	AbilitySystemComponent = InASC;
	if (!AbilitySystemComponent) return;

	// 从 ASC 获取 HealthSet
	HealthSet = AbilitySystemComponent->GetSet<UDark_TdoreHealthSet>();
	if (!HealthSet)
	{
		UE_LOG(LogDark_TdoreGAS, Error, TEXT("HealthComponent: [%s] 找不到 HealthSet"), *GetNameSafe(Owner));
		return;
	}

	// 绑定属性变化委托
	HealthSet->OnHealthChanged.AddUObject(this, &ThisClass::HandleHealthChanged);
	HealthSet->OnMaxHealthChanged.AddUObject(this, &ThisClass::HandleMaxHealthChanged);
	HealthSet->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);

	// Health = MaxHealth（安全兜底：若 MaxHealth 被 GE 改过，确保 Health 不超过新上限）
	UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthComponent] 初始化血量: Health=%.1f → MaxHealth=%.1f → SetNumericAttributeBase"),
		GetHealth(), GetMaxHealth());
	AbilitySystemComponent->SetNumericAttributeBase(UDark_TdoreHealthSet::GetHealthAttribute(), GetMaxHealth());

	// ===== 注册 VerbMessage 监听器 =====
	// 订阅 "Gameplay.Damage.Message" 频道，接收全局伤害事件
	// 参考 Lyra ULyraDamageLogDebuggerComponent
	{
		UGameplayMessageSubsystem& MessageSubsystem = UGameplayMessageSubsystem::Get(this);
		DamageListenerHandle = MessageSubsystem.RegisterListener(
			FGameplayTag::RequestGameplayTag(TEXT("Gameplay.Damage.Message")),
			this,
			&ThisClass::OnDamageVerbMessage);
	}

	ClearGameplayTags();

	OnHealthChanged.Broadcast(this, GetHealth(), GetHealth(), nullptr);
	OnMaxHealthChanged.Broadcast(this, GetMaxHealth(), GetMaxHealth(), nullptr);
}

void UDark_TdoreHealthComponent::UninitializeFromAbilitySystem()
{
	ClearGameplayTags();

	// 解除 VerbMessage 监听器
	// 注意：退出 PIE 时 Subsystem 可能已被销毁，直接调用 handle.Unregister() 更安全
	if (DamageListenerHandle.IsValid())
	{
		DamageListenerHandle.Unregister();
	}

	if (HealthSet)
	{
		HealthSet->OnHealthChanged.RemoveAll(this);
		HealthSet->OnMaxHealthChanged.RemoveAll(this);
		HealthSet->OnOutOfHealth.RemoveAll(this);
	}

	HealthSet = nullptr;
	AbilitySystemComponent = nullptr;
}

void UDark_TdoreHealthComponent::ClearGameplayTags()
{
	if (AbilitySystemComponent)
	{
		static const FGameplayTag DyingTag = FGameplayTag::RequestGameplayTag(TEXT("Status.Death.Dying"));
		static const FGameplayTag DeadTag  = FGameplayTag::RequestGameplayTag(TEXT("Status.Death.Dead"));
		AbilitySystemComponent->SetLooseGameplayTagCount(DyingTag, 0);
		AbilitySystemComponent->SetLooseGameplayTagCount(DeadTag, 0);
	}
}

// ---- 属性查询 ----

float UDark_TdoreHealthComponent::GetHealth() const
{
	return HealthSet ? HealthSet->GetHealth() : 0.0f;
}

float UDark_TdoreHealthComponent::GetMaxHealth() const
{
	return HealthSet ? HealthSet->GetMaxHealth() : 0.0f;
}

float UDark_TdoreHealthComponent::GetHealthNormalized() const
{
	if (HealthSet)
	{
		const float Health = GetHealth();
		const float MaxHealth = GetMaxHealth();
		return (MaxHealth > 0.0f) ? (Health / MaxHealth) : 0.0f;
	}
	return 0.0f;
}

// ---- 属性变化回调 ----

void UDark_TdoreHealthComponent::HandleHealthChanged(AActor* Instigator, AActor* Causer, const FGameplayEffectSpec* EffectSpec, float Magnitude, float OldValue, float NewValue)
{
	UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthComponent] %s HP 变化: %.1f → %.1f (变化量: %.1f) | 来源: %s"),
		*GetNameSafe(GetOwner()),
		OldValue,
		NewValue,
		Magnitude,
		*GetNameSafe(Instigator));

	OnHealthChanged.Broadcast(this, OldValue, NewValue, Instigator);
}

void UDark_TdoreHealthComponent::HandleMaxHealthChanged(AActor* Instigator, AActor* Causer, const FGameplayEffectSpec* EffectSpec, float Magnitude, float OldValue, float NewValue)
{
	OnMaxHealthChanged.Broadcast(this, OldValue, NewValue, Instigator);
}

void UDark_TdoreHealthComponent::HandleOutOfHealth(AActor* Instigator, AActor* Causer, const FGameplayEffectSpec* EffectSpec, float Magnitude, float OldValue, float NewValue)
{
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("================================================"));
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("[HealthComponent] HandleOutOfHealth — %s 收到死亡事件！"), *GetNameSafe(GetOwner()));
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  Instigator: %s"), *GetNameSafe(Instigator));
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  Causer: %s"), *GetNameSafe(Causer));
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  GE: %s"), EffectSpec ? *GetNameSafe(EffectSpec->Def) : TEXT("无"));
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  Magnitude: %.1f"), Magnitude);

	// 参考 Lyra：通过 ASC 发送 GameplayEvent.Death，触发生前技能
	if (AbilitySystemComponent && EffectSpec)
	{
		FGameplayEventData Payload;
		Payload.EventTag = FGameplayTag::RequestGameplayTag(TEXT("GameplayEvent.Death"));
		Payload.Instigator = Instigator;
		Payload.Target = AbilitySystemComponent->GetAvatarActor();
		Payload.OptionalObject = EffectSpec->Def;
		Payload.ContextHandle = EffectSpec->GetEffectContext();
		Payload.InstigatorTags = *EffectSpec->CapturedSourceTags.GetAggregatedTags();
		Payload.TargetTags = *EffectSpec->CapturedTargetTags.GetAggregatedTags();
		Payload.EventMagnitude = Magnitude;

		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  → 发送 GameplayEvent.Death 到 ASC"));

		FScopedPredictionWindow NewScopedWindow(AbilitySystemComponent, true);
		AbilitySystemComponent->HandleGameplayEvent(Payload.EventTag, &Payload);
	}
	else
	{
		UE_LOG(LogDark_TdoreGAS, Error, TEXT("[HealthComponent] HandleOutOfHealth 失败：ASC=%s, EffectSpec=%s"),
			AbilitySystemComponent ? TEXT("有效") : TEXT("无效"),
			EffectSpec ? TEXT("有效") : TEXT("无效"));
	}
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("================================================"));
}

// ---- 死亡流程 ----

void UDark_TdoreHealthComponent::StartDeath()
{
	if (DeathState != EDeathState::NotDead) return;

	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("[HealthComponent] StartDeath — %s 进入 Dying 状态"), *GetNameSafe(GetOwner()));

	DeathState = EDeathState::DeathStarted;

	if (AbilitySystemComponent)
	{
		static const FGameplayTag DyingTag = FGameplayTag::RequestGameplayTag(TEXT("Status.Death.Dying"));
		AbilitySystemComponent->SetLooseGameplayTagCount(DyingTag, 1);
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  → 添加 Tag: Status.Death.Dying"));
	}

	OnDeathStarted.Broadcast(GetOwner());
	GetOwner()->ForceNetUpdate();
}

void UDark_TdoreHealthComponent::FinishDeath()
{
	if (DeathState != EDeathState::DeathStarted) return;

	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("[HealthComponent] FinishDeath — %s 进入 Dead 状态"), *GetNameSafe(GetOwner()));

	DeathState = EDeathState::DeathFinished;

	if (AbilitySystemComponent)
	{
		static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(TEXT("Status.Death.Dead"));
		AbilitySystemComponent->SetLooseGameplayTagCount(DeadTag, 1);
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  → 添加 Tag: Status.Death.Dead"));
	}

	OnDeathFinished.Broadcast(GetOwner());
	GetOwner()->ForceNetUpdate();
}

// ============ VerbMessage 监听回调 ============
// 每次任意角色受到伤害（HealthSet 广播 Gameplay.Damage.Message），所有 HealthComponent 都会收到
// 参考 Lyra ULyraDamageLogDebuggerComponent::OnDamageMessage

void UDark_TdoreHealthComponent::OnDamageVerbMessage(FGameplayTag Channel, const FDark_TdoreVerbMessage& Payload)
{
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("============================================"));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("[VerbMessage] 收到伤害消息!"));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  Channel:   %s"), *Channel.ToString());
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  Instigator: %s"), *GetNameSafe(Payload.Instigator.Get()));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  Target:     %s"), *GetNameSafe(Payload.Target.Get()));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  Magnitude:  %.1f"), Payload.Magnitude);
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("  监听者(Owner): %s"), *GetNameSafe(GetOwner()));
	UE_LOG(LogDark_TdoreGAS, Display, TEXT("============================================"));
}

void UDark_TdoreHealthComponent::OnRep_DeathState(EDeathState OldDeathState)
{
	const EDeathState NewDeathState = DeathState;
	DeathState = OldDeathState;

	if (OldDeathState > NewDeathState) return;

	if (OldDeathState == EDeathState::NotDead && NewDeathState >= EDeathState::DeathStarted)
	{
		StartDeath();
		if (NewDeathState == EDeathState::DeathFinished) FinishDeath();
	}
	else if (OldDeathState == EDeathState::DeathStarted && NewDeathState == EDeathState::DeathFinished)
	{
		FinishDeath();
	}
}
