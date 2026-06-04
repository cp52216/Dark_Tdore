# GAS System Guide

> 当前项目实际实现说明。以 `Source/Dark_Tdore` 代码和当前资源配置为准。

## 1. 总体结构

```
PlayerState
  -> UDark_TdoreAbilitySystemComponent
  -> UDark_TdoreHealthSet
  -> UDark_TdoreCombatSet

Character
  -> UDark_TdorePawnExtensionComponent
  -> UDark_TdoreHeroComponent
  -> UDark_TdoreEquipmentManagerComponent
  -> UDark_TdoreCharacterMovementComponent

PlayerController
  -> PostProcessInput()
     -> ASC->ProcessAbilityInput()
```

当前项目是典型 Lyra 风格：

- `ASC` 挂在 `PlayerState`
- `Character` 作为 `AvatarActor`
- 输入由 `HeroComponent` 处理
- 技能输入缓冲在 `PlayerController::PostProcessInput` 统一结算

## 2. 初始化链路

### 2.1 PlayerState

文件：[Dark_TdorePlayerState.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Player/Dark_TdorePlayerState.cpp)

启动时会做这几件事：

1. 创建 `UDark_TdoreAbilitySystemComponent`
2. 创建 `UDark_TdoreHealthSet`、`UDark_TdoreCombatSet`
3. `PostInitializeComponents()` 中调用 `ASC->InitAbilityActorInfo(this, GetPawn())`
4. `BeginPlay()` 中从 `PawnData->AbilitySets` 批量授予默认能力

默认能力授予入口：

```cpp
AbilitySet->GiveToAbilitySystem(AbilitySystemComponent, nullptr);
```

### 2.2 Character

文件：[Dark_TdoreCharacter.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Dark_TdoreCharacter.cpp)

角色构造时默认创建：

- `UDark_TdorePawnExtensionComponent`
- `UDark_TdoreHeroComponent`
- `UDark_TdoreHealthComponent`
- `UDark_TdoreEquipmentManagerComponent`
- `UDark_TdoreCharacterMovementComponent`
- `UDark_TdoreCameraComponent`

当角色被控制时：

1. `PossessedBy()`
2. 从 `PlayerState` 取 ASC
3. `PawnExtComponent->InitializeAbilitySystem(PS->GetDark_TdoreAbilitySystemComponent(), PS)`
4. 这一步会把 `AvatarActor` 修正为当前 Character

### 2.3 HeroComponent 绑定输入

文件：[Dark_TdoreHeroComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Character/Dark_TdoreHeroComponent.cpp)

输入配置来自：

- `PawnData.InputConfig`
- `DA_InputConfig`
- `IMC_Default`

当前项目的能力输入绑定方式：

- `Started` -> `Input_AbilityTagPressed`
- `Completed` -> `Input_AbilityTagReleased`

也就是说现在不会再像之前调试阶段那样按住键每帧重复触发 Pressed。

## 3. AbilitySet 的真实作用

文件：[Dark_TdoreAbilitySet.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/AbilitySystem/Dark_TdoreAbilitySet.cpp)

`UDark_TdoreAbilitySet` 做两件事：

1. 授予 `GameplayAbility`
2. 授予 `GameplayEffect`

授予能力时会把输入 Tag 写进 `Spec.GetDynamicSpecSourceTags()`：

```cpp
FGameplayAbilitySpec AbilitySpec(AbilityEntry.Ability, AbilityEntry.AbilityLevel);
AbilitySpec.SourceObject = SourceObject;
AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityEntry.InputTag);
ASC->GiveAbility(AbilitySpec);
```

所以当前项目里，输入和能力的对应关系不是写死在 Character 里，而是靠：

- `AbilitySet` 里配置 `Ability + InputTag`
- `InputConfig` 里配置 `InputAction + InputTag`

两边通过同一个 `InputTag` 对上。

## 4. 输入到技能的调用链

### 4.1 以 `Q` 技能为例

链路如下：

```text
键盘 Q
-> IMC_Default 映射到 IA_AbilityQ
-> DA_InputConfig 里 IA_AbilityQ -> InputTag.Ability.Q
-> HeroComponent::Input_AbilityTagPressed(InputTag.Ability.Q)
-> ASC::AbilityInputTagPressed(InputTag.Ability.Q)
-> InputPressedSpecHandles 入队
-> PlayerController::PostProcessInput()
-> ASC::ProcessAbilityInput()
-> TryActivateAbility()
-> GA / BP_GA 激活
```

### 4.2 以 Sprint 为例

当前 Sprint 相关资源已经是正式链路：

- `IA_Sprint`
- `InputTag.Ability.Sprint`
- `BP_GA_Sprint`
- `GA_Sprint`

链路如下：

```text
LeftShift
-> IMC_Default -> IA_Sprint
-> DA_InputConfig -> InputTag.Ability.Sprint
-> HeroComponent Pressed/Released
-> ASC::AbilityInputTagPressed / Released
-> GA_Sprint 激活 / 结束
-> MoveComp->SetSprintPressed(true/false)
-> GetMaxSpeed() 返回 600 / 500
```

## 5. 当前输入 Tag 规范

当前项目已经统一使用：

- `InputTag.Move`
- `InputTag.Look`
- `InputTag.LookMouse`
- `InputTag.Jump`
- `InputTag.Ability.Q`
- `InputTag.Ability.Weapon.2`
- `InputTag.Ability.Sprint`

不是旧文档里的 `Input.Ability.*`。

如果后面新增能力，应该继续按这个规范配：

1. `DefaultGameplayTags.ini` 增加 `InputTag.Ability.xxx`
2. `DA_InputConfig` 增加 `InputAction -> InputTag`
3. `AbilitySet` 增加 `Ability -> InputTag`
4. `IMC_Default` 给这个 `InputAction` 配实际按键

## 6. ASC 输入处理逻辑

文件：[Dark_TdoreAbilitySystemComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/AbilitySystem/Dark_TdoreAbilitySystemComponent.cpp)

当前项目输入缓冲分三类：

- `InputPressedSpecHandles`
- `InputReleasedSpecHandles`
- `InputHeldSpecHandles`

处理规则：

1. `Pressed` 时，把匹配到的 AbilitySpecHandle 放进 `Pressed` 和 `Held`
2. `Released` 时，把 Handle 放进 `Released`，并从 `Held` 移除
3. `ProcessAbilityInput()` 里统一激活和取消

其中：

- `OnInputTriggered` 类技能：按下时触发一次
- `WhileInputActive` 类技能：按下激活，松开取消

Sprint 就属于第二类。

## 7. 角色移动与 GAS 的关系

文件：[Dark_TdoreCharacterMovementComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Character/Dark_TdoreCharacterMovementComponent.cpp)

当前移动相关有两层：

### 7.1 普通移动输入

`HeroComponent::Input_Move()` 里直接调用：

```cpp
Pawn->AddMovementInput(...)
```

也就是说：

- 动画不负责位移
- 根运动目前只允许 Montage 使用
- 常规走跑跳位移由 `CharacterMovementComponent` 驱动

### 7.2 GAS 对移动速度的影响

`GetMaxSpeed()` 里现在有：

- `Gameplay.MovementStopped` -> 返回 `0`
- Walking / NavWalking 时：
  - `bWantsToSprint = true` -> `SprintSpeed`
  - 否则 -> `WalkSpeed`

当前默认值：

- `WalkSpeed = 500`
- `SprintSpeed = 600`

## 8. 装备系统如何和 GAS 接上

装备和 GAS 的连接点有两个：

1. `PawnData->AbilitySets`
   - 授予角色出生就有的能力
   - 例如 `GA_TestQ`、`GA_Death`、`BP_GA_Sprint`

2. `EquipmentDefinition->AbilitySetsToGrant`
   - 只在装备存在期间授予
   - 卸下时通过 `GrantedHandles.TakeFromAbilitySystem()` 精确回收

这也是角色常驻能力和装备临时能力的边界。

## 9. 当前文档修正点

这次已按当前项目修正以下事实：

- 输入 Tag 统一是 `InputTag.*`，不是 `Input.*`
- Ability 输入按下事件现在是 `Started`
- Character 使用的是 `UDark_TdoreCameraComponent`，不是传统 `CameraBoom + FollowCamera`
- Character 现在包含 `EquipmentManagerComponent`
- Sprint 已经走正式 GAS 链路，不在 Character 里硬写

## 10. 相关文件

- [Dark_TdorePlayerState.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Player/Dark_TdorePlayerState.cpp)
- [Dark_TdoreAbilitySet.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/AbilitySystem/Dark_TdoreAbilitySet.cpp)
- [Dark_TdoreAbilitySystemComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/AbilitySystem/Dark_TdoreAbilitySystemComponent.cpp)
- [Dark_TdoreHeroComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Character/Dark_TdoreHeroComponent.cpp)
- [Dark_TdoreCharacter.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Dark_TdoreCharacter.cpp)
- [Dark_TdoreCharacterMovementComponent.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/Character/Dark_TdoreCharacterMovementComponent.cpp)
- [GA_Sprint.cpp](/D:/UE_ProJect/Dark_Tdore/Source/Dark_Tdore/AbilitySystem/Abilities/GA_Sprint.cpp)
