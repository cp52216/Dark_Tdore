# Lyra Starter Game - Architecture Reference

> Source: `C:\Users\dell\Documents\Unreal Projects\LyraStarterGame\Source\LyraGame\`
> Lines: ~50,000-65,000 | Files: 239 .h + 233 .cpp

## 1. Experience System (模块化游戏模式核心)

Lyra 最著名的模式：通过 DataAsset 驱动整个游戏加载流程。

### 关键类

**ULyraExperienceDefinition** (`GameModes/LyraExperienceDefinition.h`)
- `TArray<FString> GameFeaturesToEnable` — 要激活的 GameFeature Plugin 列表
- `TArray<UGameFeatureAction*> Actions` — 加载/激活时执行的操作
- `TArray<TObjectPtr<ULyraExperienceActionSet>> ActionSets` — 可复用的 Action 集合
- 继承 `UPrimaryDataAsset`

**ULyraExperienceManagerComponent** (`GameModes/LyraExperienceManagerComponent.h`)
- 状态机: `Unloaded → Loading → LoadingGameFeatures → ExecutingActions → Loaded → Deactivating`
- 三级回调优先级: High / Normal / Low
- 响应 AbilitySystemComponent 就绪 / Pawn 生成事件

**ULyraUserFacingExperienceDefinition** (`GameModes/LyraUserFacingExperienceDefinition.h`)
- `MapID`, `ExperienceID` — 映射到具体 Experience
- `TileTitle/SubTitle/Description` — UI 显示
- `TileIcon` — 缩略图
- `MaxPlayerCount`

### 模块依赖
```
LyraGameModule → ModularGameplay, ModularGameplayActors, GameFeatures, DataRegistry
```

---

## 2. GAS Integration (能力系统)

### 能力基类

**ULyraGameplayAbility** (`AbilitySystem/LyraGameplayAbility.h`)

Activation Policy (何时激活):
```cpp
ELyraAbilityActivationPolicy
  - OnInputTriggered   // 按下即触发 (跳跃)
  - WhileInputActive   // 按住持续 (冲刺)
  - OnSpawn            // Pawn 生成时自动激活
```

Activation Group (互斥控制):
```cpp
ELyraAbilityActivationGroup
  - Independent                 // 独立运行
  - Exclusive_Replaceable      // 互斥可替换
  - Exclusive_Blocking         // 互斥阻塞
```

关键属性:
```cpp
- TMap<FGameplayTag, FLyraAbilityMontageMessage> TagToMontageMap  // Tag→Montage 映射
- FCameraModeFinder CameraMode  // 切换相机模式
- TArray<ULyraAbilityCost*> AdditionalCosts  // 消耗系统 (法力/弹药/冷却)
```

### ASC 扩展

**ULyraAbilitySystemComponent** (`AbilitySystem/LyraAbilitySystemComponent.h`)
```cpp
- CancelAbilitiesByFunc(TFunctionRef)  // 按条件取消能力
- AddAbilityToActivationGroup()         // 激活组管理
- CancelActivationGroupAbilities()      // 取消同组能力
- AbilityInputTagPressed/Released()     // 输入缓冲 (可预测)
- ProcessAbilityInput()                 // 处理缓冲输入
```

### 能力集合

**ULyraAbilitySet** (`AbilitySystem/LyraAbilitySet.h`)
```cpp
- TArray<FLyraAbilitySet_GameplayAbility> GrantedGameplayAbilities
- TArray<FLyraAbilitySet_GameplayEffect>  GrantedGameplayEffects
- TArray<FLyraAbilitySet_AttributeSet>    GrantedAttributes
- FLyraAbilitySet_GrantedHandles (撤销用)
```

### 属性集

```cpp
ULyraHealthSet      → Health, MaxHealth, Healing, Damage
ULyraCombatSet      → BaseDamage, BaseHeal (用于 ExecutionCalculation)
ULyraAttributeSet   → 自定义属性基类
```

### Damage Execution
```cpp
ULyraDamageExecution   → FGameplayEffectExecutionScopedModifierInfo
ULyraHealExecution     → 治疗计算
```

---

## 3. Character Architecture (InitState 状态机)

### 组件初始化链

```
LyraCharacter
├── ULyraPawnExtensionComponent   (InitState 驱动)
│   ├── 等待 ASC 就绪
│   ├── 等待 PawnData 复制
│   └── 触发下一阶段
├── ULyraHeroComponent            (输入/相机/Actor 信息复制)
│   ├── EnhancedInput 映射绑定
│   ├── 能力相机模式覆盖
│   └── 输入配置数据资产
├── ULyraHealthComponent          (生命/死亡)
│   ├── HealthSet 监听
│   ├── DamageLog 记录
│   └── 死亡序列播放
└── UCameraComponent
```

### InitState 接口

**IGameFrameworkInitStateInterface**:
```
InitState_Spawned → InitState_DataAvailable → InitState_DataInitialized → InitState_GameplayReady
```

### PawnData

**ULyraPawnData** (`Character/LyraPawnData.h`)
```cpp
- TSubclassOf<APawn> PawnClass
- TArray<ULyraAbilitySet*> AbilitySets
- TArray<ULyraInputConfig*> InputConfigs      // EnhancedInput 映射配置
- TArray<ULyraCameraMode*> DefaultCameraModes
- ULyraTaggedSoundBank* TaggedSoundBank
```

---

## 4. Game Feature Action System

### 基类

**UGameFeatureAction_WorldActionBase** (`GameFeatures/GameFeatureAction_WorldActionBase.h`)
```cpp
virtual void OnGameFeatureActivating()  // 插件激活时
virtual void OnGameFeatureDeactivating() // 插件停用时
virtual void AddToWorld(FWorldContext&)  // 添加到世界
```

### 关键 Actions

```
GameFeatureAction_AddAbilities          → 按 ActorClass 授予能力/属性/AbilitySet
GameFeatureAction_AddInputContextMapping → 添加 EnhancedInput Context
GameFeatureAction_AddComponents          → 添加 Actor 组件
GameFeatureAction_SplitscreenConfig      → 分屏配置
```

### 策略

**ULyraGameFeaturePolicy** (`GameFeatures/LyraGameFeaturePolicy.h`)
```cpp
- 继承 UDefaultGameFeaturesProjectPolicies
- GetGameFeatureLoadingMode() — 控制加载模式
- GetPreloadedAssetPaths() — 预加载列表
- AddBuiltInGameFeaturePlugins() — 内置 GameFeature 插件
```

---

## 5. GameMode / GameState

```cpp
ALyraGameMode : AModularGameModeBase
  - 通过 Experience 驱动初始化
  - OnExperienceLoaded()
  - InitGameState() → 触发 Experience 加载
  - Bot 创建

ALyraGameState : AModularGameStateBase, IAbilitySystemInterface
  - ULyraExperienceManagerComponent  (Experience 加载)
  - ULyraAbilitySystemComponent      (全局 GameplayCue)
  - 多播 VerbMessage
```

---

## 6. Enhanced Input System

**ULyraInputConfig** (`Input/LyraInputConfig.h`)
```cpp
- TArray<FLyraInputAction> NativeInputActions    → 原生 C++ 绑定
- TArray<FLyraInputAction> AbilityInputActions    → GAS 能力绑定
- FLyraInputAction: { UInputAction, FGameplayTag }
```

**ULyraHeroComponent** Input 处理:
```cpp
- Input_AbilityInputTagPressed(FGameplayTag)
- Input_AbilityInputTagReleased(FGameplayTag)
- Input_Move(const FInputActionValue&)
- Input_LookMouse/LookStick(const FInputActionValue&)
```

---

## 7. Equipment / Inventory

```
ULyraEquipmentDefinition  → 装备 DataAsset
ULyraEquipmentInstance     → 运行时装备实例 (可授权能力)
ULyraEquipmentManagerComponent → 管理装备槽位
ULyraQuickBarComponent     → 快速栏 (武器切换)

ULyraInventoryItemDefinition → 物品 DataAsset
ULyraInventoryItemInstance   → 运行时实例
ULyraInventoryManagerComponent → 物品管理
IPickupable                   → 拾取接口
```

---

## 8. Teams System

```cpp
ULyraTeamSubsystem : UWorldSubsystem
  - 队伍分配
  - CompareTeams() → 队伍比较
  - CanCauseDamage()

ULyraTeamDisplayAsset → UI 层面的队伍显示 (颜色、图标)
ULyraTeamStatics      → 蓝图工具库
```

---

## 9. Messages (Verb-based)

```cpp
FLyraVerbMessage
  - FGameplayTag Verb    // 动作类型 (Killed, Revived, PickedUp)
  - AActor* Instigator
  - AActor* Target
  - FGameplayTagContainer InstigatorTags
  - double Magnitude

ALyraGameState::MulticastMessage_Unreliable/Reliable()
```

---

## 10. Common UI Stack

```
Lyra 使用 CommonGame 插件的 UI 框架:
- UCommonActivatableWidget → Focus/Input 管理
- Activatable Stack → 分层管理 (Menu > Game > HUD)
- CommonInput → 跨平台输入模式切换
- CommonUser → 本地玩家管理
```

---

## File Index (按功能域搜索)

| 搜索词 | 相关文件 |
|--------|----------|
| Experience | `LyraExperienceDefinition.h`, `LyraExperienceManagerComponent.h`, `LyraExperienceActionSet.h` |
| GAS/Ability | `LyraGameplayAbility.h`, `LyraAbilitySystemComponent.h`, `LyraAbilitySet.h`, `LyraGlobalAbilitySystem.h` |
| Character | `LyraCharacter.h`, `LyraPawnExtensionComponent.h`, `LyraHeroComponent.h`, `LyraPawnData.h` |
| Health/Damage | `LyraHealthComponent.h`, `LyraHealthSet.h`, `LyraCombatSet.h`, `LyraDamageExecution.h` |
| GameFeature | `LyraGameFeaturePolicy.h`, `GameFeatureAction_WorldActionBase.h`, `GameFeatureAction_AddAbilities.h` |
| Input | `LyraInputConfig.h`, `LyraInputComponent.h`, `LyraInputModifiers.h` |
| Inventory | `LyraInventoryManagerComponent.h`, `LyraInventoryItemDefinition.h`, `LyraQuickBarComponent.h` |
| Equipment | `LyraEquipmentDefinition.h`, `LyraEquipmentInstance.h`, `LyraEquipmentManagerComponent.h` |
| Team | `LyraTeamSubsystem.h`, `LyraTeamDisplayAsset.h`, `LyraTeamStatics.h` |
| UI | `LyraHUDLayout.h`, `LyraIndicatorManagerComponent.h`, `LyraUIManagerSubsystem.h` |
| Messages | `LyraVerbMessage.h` |
| Settings | `LyraSettingsLocal.h`, `LyraGameSettingRegistry.h` |
| Camera | `LyraCameraComponent.h`, `LyraCameraMode_ThirdPerson.h` |
| GamePhase | `LyraGamePhaseSubsystem.h`, `LyraGamePhaseAbility.h` |
| Replication | `LyraReplicationGraph.h` |
