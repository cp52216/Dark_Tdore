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

### PlayerState

1. 创建 `UDark_TdoreAbilitySystemComponent`
2. 创建 `UDark_TdoreHealthSet`、`UDark_TdoreCombatSet`
3. `PostInitializeComponents()` 中调用 `ASC->InitAbilityActorInfo(this, GetPawn())`
4. `BeginPlay()` 中从 `PawnData->AbilitySets` 批量授予默认能力

### Character

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
3. `PawnExtComponent->InitializeAbilitySystem(PS->ASC, PS)`
4. 这一步会把 `AvatarActor` 修正为当前 Character

## 3. AbilitySet 的真实作用

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

输入和能力的对应关系不写死在 Character 里，而是靠：
- `AbilitySet` 里配置 `Ability + InputTag`
- `InputConfig` 里配置 `InputAction + InputTag`
两边通过同一个 `InputTag` 对上。

## 4. 输入到技能的调用链

以 `Q` 技能为例：

```
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

## 5. 当前输入 Tag 规范

- `InputTag.Move`
- `InputTag.Look`
- `InputTag.LookMouse`
- `InputTag.Jump`
- `InputTag.Ability.Q`
- `InputTag.Ability.Weapon.2`
- `InputTag.Ability.Sprint`

新增能力步骤：
1. `DefaultGameplayTags.ini` 增加 `InputTag.Ability.xxx`
2. `DA_InputConfig` 增加 `InputAction -> InputTag`
3. `AbilitySet` 增加 `Ability -> InputTag`
4. `IMC_Default` 给这个 `InputAction` 配实际按键

## 6. ASC 输入处理逻辑

当前项目输入缓冲分三类：
- `InputPressedSpecHandles`
- `InputReleasedSpecHandles`
- `InputHeldSpecHandles`

处理规则：
1. `Pressed` 时，把匹配到的 Handle 放进 `Pressed` 和 `Held`
2. `Released` 时，把 Handle 放进 `Released`，并从 `Held` 移除
3. `ProcessAbilityInput()` 里统一激活和取消

其中：
- `OnInputTriggered` 类技能：按下时触发一次
- `WhileInputActive` 类技能：按下激活，松开取消（如 Sprint）

## 7. 装备系统如何和 GAS 接上

装备和 GAS 的连接点有两个：
1. `PawnData->AbilitySets`：授予角色出生就有的能力（如 `GA_TestQ`、`GA_Death`）
2. `EquipmentDefinition->AbilitySetsToGrant`：只在装备存在期间授予，卸下时通过 `GrantedHandles.TakeFromAbilitySystem()` 精确回收

## 8. 相关文件

- `Player/Dark_TdorePlayerState.cpp`
- `AbilitySystem/Dark_TdoreAbilitySet.cpp`
- `AbilitySystem/Dark_TdoreAbilitySystemComponent.cpp`
- `Character/Dark_TdoreHeroComponent.cpp`
- `Dark_TdoreCharacter.cpp`
- `Character/Dark_TdoreCharacterMovementComponent.cpp`
- `AbilitySystem/Abilities/GA_Sprint.cpp`
