// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Combat/Dark_TdoreCombatTypes.h"
#include "Equipment/Dark_TdoreGameplayAbility_FromEquipment.h"
#include "Dark_TdoreGameplayAbility_MeleeCombo.generated.h"

class UDark_TdoreCombatInputBufferComponent;
class UDark_TdoreWeaponInstance;

UCLASS(Abstract, Blueprintable)
class UDark_TdoreGameplayAbility_MeleeCombo : public UDark_TdoreGameplayAbility_FromEquipment
{
	GENERATED_BODY()

public:
	UDark_TdoreGameplayAbility_MeleeCombo(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	UDark_TdoreWeaponInstance* GetWeaponInstance() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	UDark_TdoreCombatInputBufferComponent* GetInputBufferComponent() const;

	// 蓝图在动画预输入窗口内调用：如果玩家提前按了允许的攻击键，就返回该输入。
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool TryConsumeNextComboInput(FGameplayTagContainer AllowedInputTags, FGameplayTag& OutInputTag);

	// 根据输入标签查找下一段连招数据，便于蓝图决定跳转到哪个 Montage Section。
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool FindComboStepForInput(FGameplayTag InputTag, FDark_TdoreComboStep& OutStep) const;

	// 常用连招入口：从输入缓冲里消费下一段允许输入，找到对应 Step 后立刻触发 Start Combo Step。
	UFUNCTION(BlueprintCallable, Category = "Combat")
	bool TryStartNextComboStepFromBuffer(const FGameplayTagContainer& AllowedInputTags, FDark_TdoreComboStep& OutStep);

protected:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		OUT FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", DisplayName = "Start Combo Step")
	void K2_StartComboStep(const FDark_TdoreComboStep& Step);

private:
	// 武器连招配置。不同武器换不同 DataAsset，Ability 逻辑保持通用。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDark_TdoreComboData> ComboData;

	// 起手段名字。当前输入路由不携带 TriggerEventData，所以第一版用默认起手段。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FName DefaultStepName;
};
