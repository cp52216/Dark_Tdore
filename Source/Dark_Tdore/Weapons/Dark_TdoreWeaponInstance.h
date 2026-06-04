// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Cosmetics/Dark_TdoreCosmeticAnimationTypes.h"
#include "Equipment/Dark_TdoreEquipmentInstance.h"
#include "Dark_TdoreWeaponInstance.generated.h"

class UAnimInstance;
class UAnimMontage;

/**
 * 武器装备实例。
 *
 * 这是 Dark_Tdore 的武器运行时基类，继承装备实例。
 * 当前先保留 Lyra 武器模块最关键的通用能力：
 * - 记录装备时间。
 * - 记录最近一次开火/攻击时间。
 * - 提供动画层选择入口。
 *
 * 剑、拳套、枪械都可以继续派生自它。
 */
UCLASS(BlueprintType, Blueprintable)
class UDark_TdoreWeaponInstance : public UDark_TdoreEquipmentInstance
{
	GENERATED_BODY()

public:
	UDark_TdoreWeaponInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void OnEquipped() override;
	virtual void OnUnequipped() override;

	/** 武器攻击/开火时调用，用于刷新最近交互时间。近战攻击也可以复用这个接口。 */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void UpdateFiringTime();

	/** 距离最近一次装备或攻击经过了多久，可用于 UI、准星扩散、冷却表现等。 */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	float GetTimeSinceLastInteractedWith() const;

	/** 根据是否装备返回动画层类，例如装备剑时返回 ABP_ItemAnimLayers_Sword。 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Animation")
	TSubclassOf<UAnimInstance> PickAnimLayer(bool bEquipped) const;

	/** Lyra 风格动画层选择：后续可以根据 CosmeticTags 区分男性/女性、不同体型或皮肤版本的武器动画层。 */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "Animation")
	TSubclassOf<UAnimInstance> PickBestAnimLayer(bool bEquipped, const FGameplayTagContainer& CosmeticTags) const;

protected:
	/** 装备该武器时使用的动画层选择集。Sword 的默认层填 ABP_ItemAnimLayers_Sword。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	FDark_TdoreAnimLayerSelectionSet EquippedAnimSet;

	/** 卸下该武器或回到空手时使用的动画层选择集。当前收刀/空手动画层暂时可以不填。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	FDark_TdoreAnimLayerSelectionSet UnequippedAnimSet;

	/** 装备时可选播放的拔刀 Montage。现在可以留空，后续有资源后直接在蓝图子类里填。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> EquipMontage = nullptr;

	/** 卸下时可选播放的收刀 Montage。现在可以留空，后续有资源后直接在蓝图子类里填。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UAnimMontage> UnequipMontage = nullptr;

	/** 把选择到的动画层 Link 到角色 Mesh 上；客户端收到装备复制时也会执行。返回值表示本次是否有有效动画层。 */
	bool LinkAnimLayer(bool bEquipped);

	/** 装备/卸下时同步 ASC 上的通用武器状态标签，供 ABP_Character_Base 的 GameplayTagPropertyMap 读取。 */
	void SetWeaponEquippedTag(bool bEquipped) const;

	/** 播放拔刀/收刀 Montage。资源暂时为空时什么都不做。 */
	void PlayWeaponMontage(UAnimMontage* MontageToPlay) const;

private:
	/** 最近一次装备时间，使用 World 时间秒。 */
	double TimeLastEquipped = 0.0;
	/** 最近一次攻击/开火时间，使用 World 时间秒。 */
	double TimeLastFired = 0.0;

	/** 当前已经 Link 到角色 Mesh 的动画层。卸下时用它精确 Unlink，避免残留剑动画层。 */
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> LinkedAnimLayer;
};
