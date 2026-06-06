// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Combat/Dark_TdoreCombatTypes.h"
#include "Equipment/Dark_TdoreGameplayAbility_FromEquipment.h"
#include "TimerManager.h"
#include "Dark_TdoreGameplayAbility_MeleeCombo.generated.h"

class UDark_TdoreCombatInputBufferComponent;
class UDark_TdoreWeaponInstance;

/**
 * 通用近战连招 Ability。
 *
 * 设计方向参考 GA_Sprint：Ability 自己只负责一条清晰的玩法职责。
 * 这里不写具体武器招式顺序，而是从 ComboData 读取连招段；
 * 不依赖 Character 代码，而是通过装备实例和 Pawn 上的输入缓冲组件工作。
 */
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

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// C++ 默认播放当前 Step 的 Montage；蓝图事件只负责扩展表现，例如镜头、特效、调试输出。
	void StartComboStep(const FDark_TdoreComboStep& Step);

	UFUNCTION(BlueprintImplementableEvent, Category = "Combat", DisplayName = "On Combo Step Started")
	void K2_StartComboStep(const FDark_TdoreComboStep& Step);

private:
	void BindInputBufferDelegates();
	void UnbindInputBufferDelegates();
	void HandleBufferedInput(FGameplayTag InputTag);
	void HandleInputBufferWindowOpened(FName WindowName);
	void OpenDefaultInputBufferWindow();
	void CloseDefaultInputBufferWindow();
	bool TryStartNextComboStep();
	bool FindNextComboStepForInput(FGameplayTag InputTag, FDark_TdoreComboStep& OutStep) const;
	void HandleComboStepFinished();

private:
	// 武器连招配置。不同武器换不同 DataAsset，Ability 逻辑保持通用。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDark_TdoreComboData> ComboData;

	// 起手段名字。当前输入路由不携带 TriggerEventData，所以第一版用默认起手段。
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	FName DefaultStepName;

	// 当前段播放结束的兜底计时器。后续接 AbilityTask_PlayMontageAndWait 后可替换为任务回调。
	FTimerHandle ComboStepFinishTimerHandle;

	// 没有动画通知时使用的默认预输入窗口计时器。
	FTimerHandle DefaultInputBufferWindowOpenTimerHandle;
	FTimerHandle DefaultInputBufferWindowCloseTimerHandle;

	// 记录 C++ 默认预输入窗口是否已经打开，切下一段或结束技能时要配对关闭，避免 OpenCount 叠加。
	UPROPERTY(Transient)
	bool bDefaultInputBufferWindowOpen = false;

	// 当前连招段。重复左键时，需要从当前段之后查下一段，而不是按 InputTag 永远查到 Light_01。
	UPROPERTY(Transient)
	FDark_TdoreComboStep CurrentStep;

	UPROPERTY(Transient)
	bool bHasCurrentStep = false;
};
