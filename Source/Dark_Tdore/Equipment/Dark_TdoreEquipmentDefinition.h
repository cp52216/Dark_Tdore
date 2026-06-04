// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Dark_TdoreEquipmentDefinition.generated.h"

class AActor;
class UDark_TdoreAbilitySet;
class UDark_TdoreEquipmentInstance;

USTRUCT(BlueprintType)
struct FDark_TdoreEquipmentActorToSpawn
{
	GENERATED_BODY()

	/** 装备时要生成的 Actor 类型，例如剑模型 Actor、盾牌 Actor、特效挂件等。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	TSubclassOf<AActor> ActorToSpawn;

	/** 生成后要挂到角色 Mesh 的插槽名，例如 hand_rSocket、spine_03Socket。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FName AttachSocket;

	/** 挂接后的相对变换，用来微调武器在插槽上的位置、旋转和缩放。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Equipment")
	FTransform AttachTransform;
};

/**
 * 装备定义，参考 Lyra EquipmentDefinition 的项目化版本。
 *
 * 它是“配置资产/蓝图类”，描述一件装备会做什么：
 * - 创建哪种 EquipmentInstance
 * - 装备时授予哪些 AbilitySet
 * - 装备时在角色身上生成哪些可见 Actor
 *
 * 用法：
 * 1. 创建蓝图类，父类选 Dark_TdoreEquipmentDefinition。
 * 2. 设置 InstanceType，例如 BP_WeaponInstance_Sword。
 * 3. 设置 AbilitySetsToGrant，例如剑攻击/格挡 AbilitySet。
 * 4. 设置 ActorsToSpawn，例如剑模型并挂到 hand_rSocket。
 */
UCLASS(Blueprintable, Const, Abstract, BlueprintType)
class UDark_TdoreEquipmentDefinition : public UObject
{
	GENERATED_BODY()

public:
	UDark_TdoreEquipmentDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 装备时创建的运行时实例类型；为空时由管理组件回退到 Dark_TdoreEquipmentInstance。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType;

	/**
	 * 装备期间临时授予 ASC 的 AbilitySet。
	 *
	 * 这些技能/GE 会在装备时授予，在卸下时通过 GrantedHandles 自动回收。
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TArray<TObjectPtr<const UDark_TdoreAbilitySet>> AbilitySetsToGrant;

	/** 装备时生成并挂接到 Pawn/Character Mesh 上的 Actor 列表。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Equipment")
	TArray<FDark_TdoreEquipmentActorToSpawn> ActorsToSpawn;
};
