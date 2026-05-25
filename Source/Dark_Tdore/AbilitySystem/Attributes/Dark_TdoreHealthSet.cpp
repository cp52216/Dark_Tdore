// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dark_TdoreHealthSet.h"
#include "Dark_TdoreAttributeSet.h"
#include "AbilitySystem/Dark_TdoreAbilitySystemComponent.h"
#include "Dark_TdoreLogChannels.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Dark_TdoreHealthSet)

// ============ GameplayTag 定义 ============

UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_DamageImmunity, "Gameplay.DamageImmunity");
UE_DEFINE_GAMEPLAY_TAG(TAG_Gameplay_DamageSelfDestruct, "Gameplay.Damage.SelfDestruct");

// ============ 构造 ============

UDark_TdoreHealthSet::UDark_TdoreHealthSet()
	: Health(100.0f)
	, MaxHealth(100.0f)
{
	bOutOfHealth = false;
	MaxHealthBeforeAttributeChange = 0.0f;
	HealthBeforeAttributeChange = 0.0f;

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthSet] 构造: Health=%.1f MaxHealth=%.1f"), GetHealth(), GetMaxHealth());
}

// ============ 网络复制 ============

void UDark_TdoreHealthSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UDark_TdoreHealthSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UDark_TdoreHealthSet, MaxHealth, COND_None, REPNOTIFY_Always);
}

void UDark_TdoreHealthSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDark_TdoreHealthSet, Health, OldValue);

	const float CurrentHealth = GetHealth();
	const float EstimatedMagnitude = CurrentHealth - OldValue.GetCurrentValue();

	UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthSet] OnRep_Health: %.1f → %.1f"), OldValue.GetCurrentValue(), CurrentHealth);

	OnHealthChanged.Broadcast(nullptr, nullptr, nullptr, EstimatedMagnitude, OldValue.GetCurrentValue(), CurrentHealth);

	if (!bOutOfHealth && CurrentHealth <= 0.0f)
	{
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("[HealthSet] OnRep_Health — 客户端血量归零!"));
		OnOutOfHealth.Broadcast(nullptr, nullptr, nullptr, EstimatedMagnitude, OldValue.GetCurrentValue(), CurrentHealth);
	}

	bOutOfHealth = (CurrentHealth <= 0.0f);
}

void UDark_TdoreHealthSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UDark_TdoreHealthSet, MaxHealth, OldValue);

	OnMaxHealthChanged.Broadcast(nullptr, nullptr, nullptr, GetMaxHealth() - OldValue.GetCurrentValue(), OldValue.GetCurrentValue(), GetMaxHealth());
}

// ============ GE 执行前 — 伤害免疫检查 ============

bool UDark_TdoreHealthSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data))
	{
		return false;
	}

	// 仅处理伤害属性
	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		if (Data.EvaluatedData.Magnitude > 0.0f)
		{
			const bool bIsDamageFromSelfDestruct = Data.EffectSpec.GetDynamicAssetTags().HasTagExact(TAG_Gameplay_DamageSelfDestruct);

			// 伤害免疫 = 非自毁伤害直接归零
			if (Data.Target.HasMatchingGameplayTag(TAG_Gameplay_DamageImmunity) && !bIsDamageFromSelfDestruct)
			{
				UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthSet] 伤害免疫 — 伤害归零: %s"), *GetNameSafe(Data.Target.GetOwner()));
				Data.EvaluatedData.Magnitude = 0.0f;
				return false;
			}
		}
	}

	// 保存变化前的值（用于 PostGameplayEffectExecute 判断是否需要广播）
	HealthBeforeAttributeChange = GetHealth();
	MaxHealthBeforeAttributeChange = GetMaxHealth();

	return true;
}

// ============ GE 执行后 — Meta 属性转换 ============

void UDark_TdoreHealthSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	const float MinimumHealth = 0.0f;
	const FGameplayEffectContextHandle& EffectContext = Data.EffectSpec.GetEffectContext();
	AActor* Instigator = EffectContext.GetOriginalInstigator();
	AActor* Causer = EffectContext.GetEffectCauser();

	// ===== 伤害 → 扣血 =====
	if (Data.EvaluatedData.Attribute == GetDamageAttribute())
	{
		const float LocalDamage = GetDamage();
		if (LocalDamage > 0.0f)
		{
			UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthSet] 收到 %.1f 伤害: HP %.1f → %.1f | 来源: %s"),
				LocalDamage, GetHealth(), FMath::Clamp(GetHealth() - LocalDamage, MinimumHealth, GetMaxHealth()), *GetNameSafe(Instigator));
		}

		SetHealth(FMath::Clamp(GetHealth() - GetDamage(), MinimumHealth, GetMaxHealth()));
		SetDamage(0.0f);
	}
	// ===== 治疗 → 加血 =====
	else if (Data.EvaluatedData.Attribute == GetHealingAttribute())
	{
		const float LocalHealing = GetHealing();
		if (LocalHealing > 0.0f)
		{
			UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthSet] 收到 %.1f 治疗: HP %.1f → %.1f"),
				LocalHealing, GetHealth(), FMath::Clamp(GetHealth() + LocalHealing, MinimumHealth, GetMaxHealth()));
		}

		SetHealth(FMath::Clamp(GetHealth() + GetHealing(), MinimumHealth, GetMaxHealth()));
		SetHealing(0.0f);
	}
	// ===== Health 直接修改 → Clamp =====
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), MinimumHealth, GetMaxHealth()));
	}
	// ===== MaxHealth 变化 → 广播 =====
	else if (Data.EvaluatedData.Attribute == GetMaxHealthAttribute())
	{
		UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthSet] MaxHealth 变化: %.1f → %.1f (GE: %s)"),
			MaxHealthBeforeAttributeChange, GetMaxHealth(), *GetNameSafe(Data.EffectSpec.Def));
		OnMaxHealthChanged.Broadcast(Instigator, Causer, &Data.EffectSpec, Data.EvaluatedData.Magnitude, MaxHealthBeforeAttributeChange, GetMaxHealth());
	}

	// ===== Health 实际变化 → 广播 =====
	if (GetHealth() != HealthBeforeAttributeChange)
	{
		OnHealthChanged.Broadcast(Instigator, Causer, &Data.EffectSpec, Data.EvaluatedData.Magnitude, HealthBeforeAttributeChange, GetHealth());
	}

	// ===== 死亡检测 =====
	UE_LOG(LogDark_TdoreGAS, Warning, TEXT("[HealthSet] 死亡检测: Health=%.1f, bOutOfHealth=%d, Damage=%.1f"), GetHealth(), bOutOfHealth, GetDamage());
	if ((GetHealth() <= 0.0f) && !bOutOfHealth)
	{
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("================================================"));
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("[HealthSet] 血量归零 — %s 已死亡！"), *GetNameSafe(Data.Target.GetOwner()));
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("  Instigator: %s  Causer: %s  GE: %s"), *GetNameSafe(Instigator), *GetNameSafe(Causer), *GetNameSafe(Data.EffectSpec.Def));
		UE_LOG(LogDark_TdoreGAS, Warning, TEXT("================================================"));

		OnOutOfHealth.Broadcast(Instigator, Causer, &Data.EffectSpec, Data.EvaluatedData.Magnitude, HealthBeforeAttributeChange, GetHealth());
	}

	bOutOfHealth = (GetHealth() <= 0.0f);
}

// ============ PreAttributeBaseChange / PreAttributeChange — Clamp ============

void UDark_TdoreHealthSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UDark_TdoreHealthSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

// ============ PostAttributeChange — MaxHealth 降低时 Clamp Health ============

void UDark_TdoreHealthSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	// MaxHealth 降低时，当前 Health 不能超过新上限
	if (Attribute == GetMaxHealthAttribute())
	{
		if (GetHealth() > NewValue)
		{
			UDark_TdoreAbilitySystemComponent* DarkASC = GetDarkTdoreAbilitySystemComponent();
			check(DarkASC);
			DarkASC->ApplyModToAttribute(GetHealthAttribute(), EGameplayModOp::Override, NewValue);
		}
	}

	// 复活检测
	if (bOutOfHealth && (GetHealth() > 0.0f))
	{
		bOutOfHealth = false;
		UE_LOG(LogDark_TdoreGAS, Log, TEXT("[HealthSet] %s 已复活！HP: %.1f"), *GetNameSafe(GetOwningActor()), GetHealth());
	}
}

// ============ Clamp ============

void UDark_TdoreHealthSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		// 不允许生命值为负数或超过最大生命值
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// 不允许最大生命值低于 1
		NewValue = FMath::Max(NewValue, 1.0f);
	}
}
