# Equipment System Guide

> 当前项目装备系统的真实设计和调用链说明。重点覆盖“按键 -> GA -> EquipmentManager -> WeaponInstance -> 动画层/Tag”的完整流程。

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

文件：[Dark_TdoreEquipmentDefinition.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentDefinition.h)

定义“这件装备是什么配置”。

它负责描述：

- 运行时要创建哪种 `EquipmentInstance`
- 装备期间要授予哪些 `AbilitySet`
- 装备时要生成哪些挂件 Actor

关键字段：

- `InstanceType`
- `AbilitySetsToGrant`
- `ActorsToSpawn`

### 2.2 EquipmentInstance

文件：[Dark_TdoreEquipmentInstance.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentInstance.h)

这是“某个角色当前身上的这件装备实例”。

它负责：

- 保存 `Instigator`
- 保存生成出来的挂件 Actor
- 提供 `OnEquipped / OnUnequipped`
- 支持网络复制

### 2.3 WeaponInstance

文件：[Dark_TdoreWeaponInstance.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Dark_TdoreWeaponInstance.h)

这是武器专用运行时实例，继承自 `EquipmentInstance`。

当前额外负责：

- 设置 `Status.Weapon.Equipped`
- Link / Unlink 动画层
- 播放装备 / 卸下 Montage
- 记录最近装备和最近攻击时间

### 2.4 EquipmentManagerComponent

文件：[Dark_TdoreEquipmentManagerComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentManagerComponent.cpp)

这是当前装备系统的管理中心。

它负责：

- 管理当前 Pawn 的装备列表
- 创建 / 销毁 `EquipmentInstance`
- 装备时授予 `AbilitySet`
- 卸下时回收 `AbilitySet`
- 生成 / 销毁挂件 Actor
- 通过 FastArray 复制装备列表

角色默认已经挂了这个组件：

[Dark_TdoreCharacter.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Dark_TdoreCharacter.cpp)

## 3. 当前角色身上的装备入口

角色构造函数里已经默认创建：

```cpp
EquipmentManagerComponent = CreateDefaultSubobject<UDark_TdoreEquipmentManagerComponent>(TEXT("EquipmentManagerComponent"));
```

所以当前所有正式装备逻辑，都应该走：

```text
Character
-> EquipmentManagerComponent
-> EquipItem / UnequipItem
```

而不是把武器状态直接硬写在 Character 上。

## 4. 装备数据的真实调用关系

当前装备链路分三层：

```text
EquipmentDefinition
-> EquipmentManagerComponent::EquipItem()
-> 创建 EquipmentInstance / WeaponInstance
-> OnEquipped()
```

展开后是：

```text
一份装备定义
-> 指定 InstanceType
-> 指定 AbilitySetsToGrant
-> 指定 ActorsToSpawn

装备时：
-> AddEntry()
-> NewObject<UDark_TdoreEquipmentInstance>(Owner, InstanceType)
-> Give AbilitySetsToGrant
-> SpawnEquipmentActors()
-> 调用 OnEquipped()
```

## 5. GA_EquipWeapon 的设计

文件：

- [GA_EquipWeapon.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Abilities/GA_EquipWeapon.h)
- [GA_EquipWeapon.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Abilities/GA_EquipWeapon.cpp)

这是正式的“按键切武器”能力。

### 5.1 设计意图

它不是“某把武器本体”，而是“某个按键触发的装备行为”。

比如：

- `BP_GA_EquipWeapon_Sword`
  - 父类：`UGA_EquipWeapon`
  - `EquipmentDefinition = 某个 Sword Definition`
  - `InputTag = InputTag.Ability.Weapon.2`

以后做 3 号、4 号武器时，只需要复制蓝图子类改配置，不需要再写新 C++。

### 5.2 当前 GA 配置

`UGA_EquipWeapon` 构造函数里当前配置：

- `ActivationPolicy = OnInputTriggered`
- `ActivationGroup = Exclusive_Replaceable`
- `NetExecutionPolicy = ServerOnly`
- `InstancingPolicy = InstancedPerActor`

含义：

- 按一下触发一次
- 同时只允许一个切武器 GA 在跑
- 装备列表只由服务器权威修改

## 6. 从按键到装备成功的完整链路

以当前剑武器为例：

```text
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

## 7. 完整函数调用流程示例：按 2 装备剑

下面这个例子按真实 C++ 函数调用顺序写。假设当前配置是：

- `IMC_Default`: `Two -> IA_Weapon2`
- `DA_InputConfig`: `IA_Weapon2 -> InputTag.Ability.Weapon.2`
- `DA_DefaultAbilitySet`: `BP_GA_EquipWeapon_Sword -> InputTag.Ability.Weapon.2`
- `BP_GA_EquipWeapon_Sword`: 父类 `UGA_EquipWeapon`，`EquipmentDefinition` 指向剑的装备定义
- 剑的 `EquipmentDefinition`: `InstanceType = UDark_TdoreWeaponInstance` 或其蓝图子类
- 剑的 `WeaponInstance`: `EquippedAnimSet` 里配置 `ABP_ItemAnimLayers_Sword`

### 7.1 输入进入 HeroComponent

玩家按下键盘 `2` 后，Enhanced Input 触发 `IA_Weapon2`。

`UDark_TdoreHeroComponent::InitializePlayerInput()` 在初始化时已经绑定过：

```cpp
EnhancedInputComponent->BindAction(
    Action.InputAction,
    ETriggerEvent::Started,
    this,
    &UDark_TdoreHeroComponent::Input_AbilityTagPressed,
    Action.InputTag);
```

因此运行时进入：

```cpp
UDark_TdoreHeroComponent::Input_AbilityTagPressed(FGameplayTag InputTag)
```

此时：

```cpp
InputTag == InputTag.Ability.Weapon.2
```

函数内部调用：

```cpp
ASC->AbilityInputTagPressed(InputTag);
```

### 7.2 ASC 找到匹配 AbilitySpec

进入：

```cpp
UDark_TdoreAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
```

ASC 遍历当前已经授予的所有 `ActivatableAbilities.Items`：

```cpp
for (const FGameplayAbilitySpec& AbilitySpec : ActivatableAbilities.Items)
{
    if (AbilitySpec.Ability && AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
    {
        InputPressedSpecHandles.AddUnique(AbilitySpec.Handle);
        InputHeldSpecHandles.AddUnique(AbilitySpec.Handle);
    }
}
```

因为 `BP_GA_EquipWeapon_Sword` 在 `AbilitySet` 中配置了同一个输入标签：

```cpp
InputTag.Ability.Weapon.2
```

所以它的 `AbilitySpec.Handle` 会被加入 `InputPressedSpecHandles`。

### 7.3 PlayerController 统一处理输入缓冲

本帧输入处理结束后，进入：

```cpp
ADark_TdorePlayerController::PostProcessInput(float DeltaTime, bool bGamePaused)
```

然后调用：

```cpp
ASC->ProcessAbilityInput(DeltaTime, bGamePaused);
```

`ProcessAbilityInput()` 里会遍历刚才放进去的 Handle：

```cpp
for (const FGameplayAbilitySpecHandle& AbilityHandle : AbilitiesToActivate)
{
    TryActivateAbility(AbilityHandle);
}
```

此时激活的是：

```text
BP_GA_EquipWeapon_Sword
```

### 7.4 进入 GA_EquipWeapon

激活后进入：

```cpp
UGA_EquipWeapon::ActivateAbility(...)
```

函数先提交能力：

```cpp
if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
{
    EndAbility(...);
    return;
}
```

然后从 AvatarActor 上找装备管理组件：

```cpp
AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
UDark_TdoreEquipmentManagerComponent* EquipmentManager =
    AvatarActor ? AvatarActor->FindComponentByClass<UDark_TdoreEquipmentManagerComponent>() : nullptr;
```

如果当前这把剑已经装备，则走 Toggle 卸下：

```cpp
if (UDark_TdoreEquipmentInstance* ExistingInstance = EquipmentManager->GetFirstInstanceOfDefinition(EquipmentDefinition))
{
    if (bToggleOffIfAlreadyEquipped)
    {
        EquipmentManager->UnequipItem(ExistingInstance);
    }

    EndAbility(...);
    return;
}
```

如果还没装备，并且配置了 `bUnequipOtherWeapons`，则先卸下当前所有武器实例：

```cpp
EquipmentManager->UnequipAllItemsOfType(UDark_TdoreWeaponInstance::StaticClass());
```

最后真正装备目标武器：

```cpp
EquipmentManager->EquipItem(EquipmentDefinition);
EndAbility(...);
```

### 7.5 EquipmentManager 创建装备实例

进入：

```cpp
UDark_TdoreEquipmentManagerComponent::EquipItem(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition)
```

它调用：

```cpp
Result = EquipmentList.AddEntry(EquipmentDefinition);
```

然后进入：

```cpp
FDark_TdoreEquipmentList::AddEntry(TSubclassOf<UDark_TdoreEquipmentDefinition> EquipmentDefinition)
```

这里会取装备定义 CDO：

```cpp
const UDark_TdoreEquipmentDefinition* EquipmentCDO = GetDefault<UDark_TdoreEquipmentDefinition>(EquipmentDefinition);
```

确定实例类型：

```cpp
TSubclassOf<UDark_TdoreEquipmentInstance> InstanceType = EquipmentCDO->InstanceType;
if (!InstanceType)
{
    InstanceType = UDark_TdoreEquipmentInstance::StaticClass();
}
```

创建运行时实例：

```cpp
NewEntry.Instance = NewObject<UDark_TdoreEquipmentInstance>(OwnerComponent->GetOwner(), InstanceType);
UDark_TdoreEquipmentInstance* Result = NewEntry.Instance;
Result->SetInstigator(OwnerComponent->GetOwner());
```

如果剑的定义里配置了装备期间授予的 `AbilitySetsToGrant`，这里会临时授予：

```cpp
for (const TObjectPtr<const UDark_TdoreAbilitySet>& AbilitySet : EquipmentCDO->AbilitySetsToGrant)
{
    if (AbilitySet)
    {
        AbilitySet->GiveToAbilitySystem(ASC, &NewEntry.GrantedHandles, Result);
    }
}
```

然后生成可见挂件 Actor：

```cpp
Result->SpawnEquipmentActors(EquipmentCDO->ActorsToSpawn);
```

最后标记 FastArray 条目需要复制：

```cpp
MarkItemDirty(NewEntry);
```

### 7.6 调用 WeaponInstance::OnEquipped

回到：

```cpp
UDark_TdoreEquipmentManagerComponent::EquipItem(...)
```

如果 `AddEntry()` 创建成功，会立即调用：

```cpp
Result->OnEquipped();
```

因为剑的实例类型是 `UDark_TdoreWeaponInstance` 或它的蓝图子类，所以实际进入：

```cpp
UDark_TdoreWeaponInstance::OnEquipped()
```

顺序如下：

```cpp
TimeLastEquipped = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

SetWeaponEquippedTag(true);
const bool bLinkedAnimLayer = LinkAnimLayer(true);
if (!bLinkedAnimLayer)
{
    SetWeaponEquippedTag(false);
}

Super::OnEquipped();
PlayWeaponMontage(EquipMontage);
```

### 7.7 设置武器状态 Tag

进入：

```cpp
UDark_TdoreWeaponInstance::SetWeaponEquippedTag(bool bEquipped) const
```

它先从 Pawn 上拿 ASC：

```cpp
APawn* OwningPawn = GetPawn();
UAbilitySystemComponent* ASC =
    OwningPawn ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningPawn) : nullptr;
```

然后设置松散 GameplayTag：

```cpp
ASC->SetLooseGameplayTagCount(TAG_Status_Weapon_Equipped, bEquipped ? 1 : 0);
```

也就是写入：

```text
Status.Weapon.Equipped
```

之后刷新动画实例状态：

```cpp
if (UDark_TdoreAnimInstance* AnimInstance = Cast<UDark_TdoreAnimInstance>(MeshComponent->GetAnimInstance()))
{
    AnimInstance->RefreshWeaponState();
}
```

### 7.8 Link 剑动画层

进入：

```cpp
UDark_TdoreWeaponInstance::LinkAnimLayer(bool bEquipped)
```

它先拿角色 Mesh：

```cpp
ACharacter* Character = Cast<ACharacter>(GetPawn());
USkeletalMeshComponent* MeshComponent = Character ? Character->GetMesh() : nullptr;
```

如果之前已经 Link 过旧层，先解除：

```cpp
if (LinkedAnimLayer)
{
    MeshComponent->UnlinkAnimClassLayers(LinkedAnimLayer);
    LinkedAnimLayer = nullptr;
}
```

装备时从 `EquippedAnimSet` 里选动画层：

```cpp
const TSubclassOf<UAnimInstance> AnimLayer = PickAnimLayer(true);
```

剑当前应该选到：

```text
ABP_ItemAnimLayers_Sword
```

最后挂到角色 Mesh 上：

```cpp
MeshComponent->LinkAnimClassLayers(AnimLayer);
LinkedAnimLayer = AnimLayer;
```

到这里，角色主动画蓝图就接入了剑的动画接口层。

### 7.9 AnimInstance 每帧确认武器状态

角色动画实例每帧会调用：

```cpp
UDark_TdoreAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
```

里面会刷新武器状态：

```cpp
RefreshWeaponState();
```

`RefreshWeaponState()` 会同时检查：

```cpp
const bool bHasWeaponFromTag =
    NativeAbilitySystemComponent && NativeAbilitySystemComponent.Get()->HasMatchingGameplayTag(TAG_Anim_Status_Weapon_Equipped);
```

以及：

```cpp
bHasWeaponFromEquipment =
    EquipmentManager->GetFirstInstanceOfType(UDark_TdoreWeaponInstance::StaticClass()) != nullptr;
```

最终：

```cpp
bHasWeapon = bHasWeaponFromTag || bHasWeaponFromEquipment;
```

所以动画层和 `bHasWeapon` 两边都会知道当前角色已经装备武器。

### 7.10 再按 2 卸下剑的函数调用流程

再次按 `2` 时，前半段输入链路完全一样：

```text
IA_Weapon2
-> InputTag.Ability.Weapon.2
-> ASC
-> BP_GA_EquipWeapon_Sword
-> UGA_EquipWeapon::ActivateAbility()
```

区别在这里：

```cpp
ExistingInstance = EquipmentManager->GetFirstInstanceOfDefinition(EquipmentDefinition)
```

这次能找到已经装备的剑实例，于是：

```cpp
EquipmentManager->UnequipItem(ExistingInstance);
```

然后进入：

```cpp
UDark_TdoreEquipmentManagerComponent::UnequipItem(UDark_TdoreEquipmentInstance* ItemInstance)
```

先调用：

```cpp
ItemInstance->OnUnequipped();
```

实际进入：

```cpp
UDark_TdoreWeaponInstance::OnUnequipped()
```

顺序是：

```cpp
SetWeaponEquippedTag(false);
LinkAnimLayer(false);
PlayWeaponMontage(UnequipMontage);
Super::OnUnequipped();
```

然后装备列表移除条目：

```cpp
EquipmentList.RemoveEntry(ItemInstance);
```

在：

```cpp
FDark_TdoreEquipmentList::RemoveEntry(UDark_TdoreEquipmentInstance* Instance)
```

里会回收装备临时授予的能力和 GE：

```cpp
Entry.GrantedHandles.TakeFromAbilitySystem(ASC);
```

销毁挂件 Actor：

```cpp
Instance->DestroyEquipmentActors();
```

从数组中删除：

```cpp
EntryIt.RemoveCurrent();
MarkArrayDirty();
```

到这里，剑被卸下，动画层解除，`Status.Weapon.Equipped` 清掉，装备临时能力也被回收。

## 8. GA_EquipWeapon::ActivateAbility 真实逻辑

### 8.1 前置检查

先做：

- `Super::ActivateAbility`
- `CommitAbility`
- 找 `EquipmentManager`
- 检查 `EquipmentDefinition`

只要其中任何一步失败，就 `EndAbility`。

### 8.2 已装备同一把武器时

当前逻辑：

```cpp
if (ExistingInstance)
{
    if (bToggleOffIfAlreadyEquipped)
    {
        EquipmentManager->UnequipItem(ExistingInstance);
    }

    EndAbility(...);
    return;
}
```

也就是：

- 如果这把武器已经在手上
- 再按一次相同快捷键
- 默认会把它收回

这就是当前 2 键“拿出 / 收回”切换行为的来源。

### 8.3 装备新武器前先卸当前武器

当前配置：

```cpp
if (bUnequipOtherWeapons)
{
    EquipmentManager->UnequipAllItemsOfType(UDark_TdoreWeaponInstance::StaticClass());
}
```

意味着当前阶段的武器槽策略是：

- 主武器槽同一时刻只保留一把 `WeaponInstance`

这是 QuickBar 成熟前的简化版，但方向是对的。

### 8.4 真正装备

最后执行：

```cpp
EquipmentManager->EquipItem(EquipmentDefinition);
```

然后立即 `EndAbility()`。

## 9. EquipmentManager 装备时做了什么

### 9.1 AddEntry()

入口：

[Dark_TdoreEquipmentManagerComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentManagerComponent.cpp)

核心步骤：

1. 根据 `EquipmentDefinition` 找到 `InstanceType`
2. `NewObject` 创建运行时实例
3. `SetInstigator`
4. 遍历 `AbilitySetsToGrant`
5. 用 `AbilitySet->GiveToAbilitySystem()` 授予临时能力
6. `SpawnEquipmentActors()`
7. `MarkItemDirty()`

### 9.2 EquipItem()

`EquipItem()` 在 `AddEntry()` 返回后：

1. 立即调用 `Result->OnEquipped()`
2. 如果走注册子对象复制，则 `AddReplicatedSubObject(Result)`

所以服务端本地表现和客户端复制表现最终都会走到 `OnEquipped()`。

## 10. 卸下时做了什么

### 10.1 UnequipItem()

逻辑顺序：

1. 从复制子对象列表移除
2. `ItemInstance->OnUnequipped()`
3. `EquipmentList.RemoveEntry(ItemInstance)`

### 10.2 RemoveEntry()

真正做的回收包括：

1. `GrantedHandles.TakeFromAbilitySystem(ASC)`
2. `DestroyEquipmentActors()`
3. 从 FastArray 里删除
4. `MarkArrayDirty()`

### 10.3 为什么要保存 GrantedHandles

因为装备授予的是“临时能力”。

如果不记住这些 Handle，卸武器时就没法精确回收：

- 武器攻击 GA
- 武器附带 GE
- 武器特殊状态

而现在这套设计已经把这件事处理干净了。

## 11. WeaponInstance::OnEquipped 真实表现链

文件：[Dark_TdoreWeaponInstance.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Dark_TdoreWeaponInstance.cpp)

当前装备时会按这个顺序执行：

1. 记录 `TimeLastEquipped`
2. `SetWeaponEquippedTag(true)`
3. `LinkAnimLayer(true)`
4. 如果动画层挂接失败，则回滚 `SetWeaponEquippedTag(false)`
5. `Super::OnEquipped()`
6. `PlayWeaponMontage(EquipMontage)`

卸下时：

1. `SetWeaponEquippedTag(false)`
2. `LinkAnimLayer(false)`
3. `PlayWeaponMontage(UnequipMontage)`
4. `Super::OnUnequipped()`

## 12. 武器状态怎样驱动动画

### 12.1 Loose GameplayTag

当前武器装备状态 Tag：

```text
Status.Weapon.Equipped
```

WeaponInstance 里通过：

```cpp
ASC->SetLooseGameplayTagCount(TAG_Status_Weapon_Equipped, bEquipped ? 1 : 0);
```

把它挂到角色 ASC 上。

### 12.2 AnimInstance 刷新

文件：[Dark_TdoreAnimInstance.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Animation/Dark_TdoreAnimInstance.cpp)

`RefreshWeaponState()` 会同时看两层：

1. ASC 上是否有 `Status.Weapon.Equipped`
2. `EquipmentManager` 里是否存在 `UDark_TdoreWeaponInstance`

最终：

```cpp
bHasWeapon = bHasWeaponFromTag || bHasWeaponFromEquipment;
```

这个设计比较稳：

- Tag 是表现驱动入口
- EquipmentManager 是兜底真实状态

### 12.3 动画层挂接

武器实例通过：

```cpp
MeshComponent->LinkAnimClassLayers(AnimLayer);
```

把武器层挂到角色主 AnimBP 上。

你当前剑的目标层就是：

```text
ABP_ItemAnimLayers_Sword
```

所以当前剑武器的表现切换链路是：

```text
武器装备
-> WeaponInstance::LinkAnimLayer(true)
-> 角色 Mesh LinkAnimClassLayers(ABP_ItemAnimLayers_Sword)
-> ABP_Character_Base 通过层接口接入剑的 Locomotion / Idle / Jump
```

## 13. 装备系统和 Input / GAS 的边界

这块最好记牢：

### 13.1 GAS 负责

- 输入路由
- 能力激活
- 装备行为授权
- 临时能力授予与回收

### 13.2 EquipmentManager 负责

- 当前装了什么
- 创建哪一个运行时实例
- 生成哪些挂件
- 卸下时如何回收

### 13.3 WeaponInstance 负责

- 武器表现
- 武器动画层
- 武器状态 Tag

这三层边界现在是清楚的，后面扩展比较安全。

## 14. 当前项目实际配置方式

当前正式配置方式应当是：

### 14.1 输入

- `IMC_Default`
- `DA_InputConfig`

例如：

- `IA_Weapon2 -> InputTag.Ability.Weapon.2`

### 14.2 默认角色能力

- `PawnData`
- `DA_DefaultAbilitySet`

这里授予常驻 GA，比如：

- `GA_TestQ`
- `GA_Death`
- `BP_GA_Sprint`
- `BP_GA_EquipWeapon_Sword`

### 14.3 装备定义

每一把武器各自有：

- 一个 `EquipmentDefinition`
- 一个 `WeaponInstance` 类型
- 一组 `AbilitySetsToGrant`
- 一组 `ActorsToSpawn`

## 15. 以后扩展新武器该怎么做

如果以后要加 3 号或 4 号武器，推荐步骤：

1. 新建一个 `EquipmentDefinition` 蓝图
2. 指定 `InstanceType`
3. 配好 `ActorsToSpawn`
4. 配好 `AbilitySetsToGrant`
5. 复制一个 `BP_GA_EquipWeapon_xxx`
6. 给这个 GA 配新的 `EquipmentDefinition`
7. 在 `AbilitySet` 里授予这个 GA，并设置对应 `InputTag.Ability.Weapon.3/4`
8. 在 `DA_InputConfig` / `IMC_Default` 里加对应按键映射

这样不需要再改 `Character`。

## 16. 当前系统边界和后续建议

### 16.1 已完成

- 正式 GAS 装备入口
- 单主武器槽切换
- 装备实例复制
- 武器状态 Tag
- 动画层联动

### 16.2 还没做满

- 背包数据层
- 快捷栏数据层
- 拾取后入包而不是直接 Equip
- 多武器槽 UI
- 装备切换冷却 / 排他规则细化

### 16.3 建议的后续演进

推荐保持这个顺序：

1. `InventoryItemDefinition`
2. `InventoryComponent`
3. `QuickBarComponent`
4. QuickBar 选择某格
5. 某格再驱动 `GA_EquipWeapon`

也就是：

```text
背包决定“拥有”
快捷栏决定“选中”
装备系统决定“当前挂到角色身上的实例”
```

## 17. 关键文件

- [Dark_TdoreEquipmentDefinition.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentDefinition.h)
- [Dark_TdoreEquipmentInstance.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentInstance.h)
- [Dark_TdoreEquipmentManagerComponent.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentManagerComponent.h)
- [Dark_TdoreEquipmentManagerComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Equipment/Dark_TdoreEquipmentManagerComponent.cpp)
- [Dark_TdoreWeaponInstance.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Dark_TdoreWeaponInstance.h)
- [Dark_TdoreWeaponInstance.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Dark_TdoreWeaponInstance.cpp)
- [GA_EquipWeapon.h](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Abilities/GA_EquipWeapon.h)
- [GA_EquipWeapon.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Weapons/Abilities/GA_EquipWeapon.cpp)
- [Dark_TdoreAbilitySet.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/AbilitySystem/Dark_TdoreAbilitySet.cpp)
- [Dark_TdoreAnimInstance.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Animation/Dark_TdoreAnimInstance.cpp)
- [Dark_TdoreCharacter.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Dark_TdoreCharacter.cpp)
