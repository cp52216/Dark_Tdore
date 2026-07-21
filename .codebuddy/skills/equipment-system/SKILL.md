# Equipment System Guide

> 当前项目装备系统的真实设计和调用链说明。重点覆盖"按键 -> GA -> EquipmentManager -> WeaponInstance -> 动画层/Tag"的完整流程。

## 1. 系统目标

当前装备系统是 Lyra 风格的轻量版，已经具备：

- 数据驱动装备定义
- 装备运行时实例
- 装备授予临时能力
- 装备生成挂件 Actor
- 武器装备时切换动画层
- 武器状态通过 GameplayTag 同步给动画系统

当前还没有完整背包 / QuickBar UI，但核心装备链路已经独立成立。

## 2. 核心类分工

### 2.1 EquipmentDefinition

定义"这件装备是什么配置"。负责描述：
- 运行时要创建哪种 `EquipmentInstance`
- 装备期间要授予哪些 `AbilitySet`
- 装备时要生成哪些挂件 Actor

关键字段：`InstanceType`、`AbilitySetsToGrant`、`ActorsToSpawn`

### 2.2 EquipmentInstance

"某个角色当前身上的这件装备实例"。负责：
- 保存 `Instigator`
- 保存生成出来的挂件 Actor
- 提供 `OnEquipped / OnUnequipped`
- 支持网络复制

### 2.3 WeaponInstance

武器专用运行时实例，继承自 `EquipmentInstance`。当前额外负责：
- 设置 `Status.Weapon.Equipped`
- Link / Unlink 动画层
- 播放装备 / 卸下 Montage
- 记录最近装备和最近攻击时间

### 2.4 EquipmentManagerComponent

当前装备系统的管理中心。负责：
- 管理当前 Pawn 的装备列表
- 创建 / 销毁 `EquipmentInstance`
- 装备时授予 `AbilitySet`，卸下时回收
- 生成 / 销毁挂件 Actor
- 通过 FastArray 复制装备列表

角色默认已经挂了这个组件（在 `Dark_TdoreCharacter.cpp` 构造中）。

## 3. 从按键到装备成功的完整链路

以当前剑武器为例：

```
键盘 2
-> IMC_Default 触发 IA_Weapon2
-> DA_InputConfig 映射到 InputTag.Ability.Weapon.2
-> HeroComponent::Input_AbilityTagPressed()
-> ASC::AbilityInputTagPressed()
-> ProcessAbilityInput()
-> 激活 BP_GA_EquipWeapon_Sword
-> UGA_EquipWeapon::ActivateAbility()
-> EquipmentManager->EquipItem(EquipmentDefinition)
-> 创建 WeaponInstance
-> WeaponInstance::OnEquipped()
-> 设置武器 Tag / 切动画层 / 播放 Montage
```

## 4. GA_EquipWeapon 的设计

它是"某个按键触发的装备行为"而不是"某把武器本体"。

### 当前 GA 配置

- `ActivationPolicy = OnInputTriggered`
- `ActivationGroup = Exclusive_Replaceable`
- `NetExecutionPolicy = ServerOnly`
- `InstancingPolicy = InstancedPerActor`

### 逻辑

```cpp
// 已装备 → Toggle 卸下
if (ExistingInstance) {
    if (bToggleOffIfAlreadyEquipped)
        EquipmentManager->UnequipItem(ExistingInstance);
    EndAbility(...);
    return;
}
// 先卸下当前武器（bUnequipOtherWeapons）
EquipmentManager->UnequipAllItemsOfType(UDark_TdoreWeaponInstance::StaticClass());
// 装备新武器
EquipmentManager->EquipItem(EquipmentDefinition);
EndAbility(...);
```

## 5. AddEntry 6 步详解

```
AddEntry(B_Sword_Definition)
│
├─ ① 读定义配置 → InstanceType
├─ ② NewObject<InstanceType>(Character) + SetInstigator
├─ ③ 授予 AbilitySetsToGrant → GiveToAbilitySystem
├─ ④ SpawnEquipmentActors → AttachToComponent
├─ ⑤ MarkItemDirty → 触发网络复制
└─ ⑥ 返回 Instance → EquipItem 调 OnEquipped
```

## 6. WeaponInstance::OnEquipped 执行顺序

```cpp
TimeLastEquipped = GetWorld()->GetTimeSeconds();
SetWeaponEquippedTag(true);
LinkAnimLayer(true);
if (!LinkAnimLayer) SetWeaponEquippedTag(false); // 回滚
Super::OnEquipped();
PlayWeaponMontage(EquipMontage);
```

## 7. 武器状态驱动动画

- **Loose GameplayTag**: `ASC->SetLooseGameplayTagCount(TAG_Status_Weapon_Equipped, 1/0)`
- **AnimInstance 双重检测**: `bHasWeapon = ASC_HasTag || EquipmentManager_HasWeaponInstance`
- **动画层挂接**: `MeshComponent->LinkAnimClassLayers(ABP_ItemAnimLayers_Sword)`

## 8. 快速理解（4 个核心类）

| 类 | 一句话 |
|----|--------|
| **Definition** | 配方：用什么实例、授予什么技能、生成什么模型 |
| **Instance** | 化身：角色身上这一件的运行时对象，负责装备/卸下回调 |
| **ManagerComponent** | 管家：完整装备流程（创建→授予→生成→动画→复制） |
| **FromEquipment** | 身份证：让技能反查"哪个装备给了我"，读取装备数据 |

### Entry 概念

角色可能同时装备多件东西（剑+盾+头盔），每件就是一条 Entry。每个 Entry 存三样：
- `EquipmentDefinition`：这是哪件装备
- `Instance`：运行时对象
- `GrantedHandles`：装备时授予的技能/GE句柄（服务器专用，卸下回收用）

Entry 是 FastArray 的复制单元：服务器 `MarkItemDirty` → 客户端收到增量。

### 完整链路

```
════════ 装备 ════════
按2 → GA_EquipWeapon → EquipItem(Definition)
    → AddEntry: 创建Instance + 授予技能 + 生成模型
    → OnEquipped: Tag + 动画层 + 拔刀
    → 网络复制

════════ 攻击 ════════
左键 → GA_MeleeCombo (继承 FromEquipment)
    → GetAssociatedEquipment() → WeaponInstance
    → 播放 Combo Montage → 动画层已挂载 → 正确输出剑动画
```

## 9. 以后扩展新武器

1. 新建 `EquipmentDefinition` 蓝图 → 指定 `InstanceType`
2. 配好 `ActorsToSpawn` 和 `AbilitySetsToGrant`
3. 复制 `BP_GA_EquipWeapon_xxx` → 配新的 `EquipmentDefinition`
4. 在 `AbilitySet` 里授予 GA，设置 `InputTag.Ability.Weapon.3/4`
5. 在 `DA_InputConfig` / `IMC_Default` 里加按键映射

## 10. 关键文件

- `Equipment/Dark_TdoreEquipmentDefinition.h`
- `Equipment/Dark_TdoreEquipmentInstance.h/.cpp`
- `Equipment/Dark_TdoreEquipmentManagerComponent.h/.cpp`
- `Weapons/Dark_TdoreWeaponInstance.h/.cpp`
- `Weapons/Abilities/GA_EquipWeapon.h/.cpp`
- `AbilitySystem/Dark_TdoreAbilitySet.cpp`
- `Animation/Dark_TdoreAnimInstance.cpp`
- `Dark_TdoreCharacter.cpp`
