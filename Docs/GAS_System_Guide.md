# GAS 系统完整加载与触发流程

> 参考 Lyra Starter Game 架构
> ASC 在 PlayerState 上，输入由 HeroComponent 处理，ProcessAbilityInput 在 PlayerController::PostProcessInput

---

## 一、架构概览

```
PlayerState (Owner, 持久)
  ├── UDark_TdoreAbilitySystemComponent   ← ASC 在此，角色死亡不丢失
  ├── UDark_TdoreHealthSet                ← 生命属性（Health, MaxHealth, Damage, Healing）
  ├── UDark_TdoreCombatSet                ← 战斗属性（BaseDamage, BaseHeal）
  └── UDark_TdoreAbilitySet               ← 通过 DataAsset 授予 GA/GE

Character (Avatar, 物理表现体)
  ├── UDark_TdorePawnExtensionComponent   ← 初始化协调器，管理 InitState
  ├── UDark_TdoreHeroComponent            ← 输入处理（参考 Lyra）
  ├── UDark_TdoreInputConfig              ← 输入配置 DataAsset
  └── Camera + MovementComponent

PlayerController
  └── PostProcessInput                    ← 调用 ASC->ProcessAbilityInput()

Dark_TdoreAbilitySystemGlobals
  └── AllocGameplayEffectContext()        ← 工厂：所有 GE 用 FDark_TdoreGameplayEffectContext
```

---

## 二、完整加载流程

### 阶段 1：PlayerState 创建

```
BP_GameMode 生成 PlayerState (BP_DarkTdorePlayerState)
  │
  ▼
ADark_TdorePlayerState::Constructor()
  ├── CreateDefaultSubobject<UDark_TdoreAbilitySystemComponent>   // 创建 ASC
  │     SetIsReplicated(true)
  │     SetReplicationMode(Mixed)
  ├── CreateDefaultSubobject<UDark_TdoreAttributeSet>             // 创建 AttributeSet
  └── SetNetUpdateFrequency(100.0f)
  │
  ▼
PostInitializeComponents()
  └── ASC->InitAbilityActorInfo(this, GetPawn())
      OwnerActor = PlayerState ✅  (GAS 网络所有者)
      AvatarActor = nullptr ❌    (Pawn 还没出生，后续修正)
  │
  ▼
BeginPlay()
  └── GrantAbilitySet()
        ├── 遍历 GrantedAbilities
        ├── FGameplayAbilitySpec(BP_GA_TestQ, Level=1)
        ├── Spec.SourceObject = SourceObject
        ├── Spec.GetDynamicSpecSourceTags().AddTag(Input.Ability.Q)  // ← Lyra 方式
        └── ASC->GiveAbility(Spec)                                   // ← 授予到 ASC
```

### 阶段 2：Character 创建

```
BP_GameMode 生成 Character (BP_ThirdPersonCharacter)
  │
  ▼
ADark_TdoreCharacter::Constructor()
  ├── SetDefaultSubobjectClass<UDark_TdoreCharacterMovementComponent>
  ├── CameraBoom + FollowCamera
  ├── PawnExtComponent = CreateDefaultSubobject<UDark_TdorePawnExtensionComponent>
  │     └── OnAbilitySystemInitialized_RegisterAndCall(OnAbilitySystemInitialized)
  └── HeroComponent = CreateDefaultSubobject<UDark_TdoreHeroComponent>
```

### 阶段 3：PossessedBy — ASC 关联 + 输入绑定

```
PlayerController::Possess(Character)
  │
  ▼
ADark_TdoreCharacter::PossessedBy(Controller)
  │
  ├── PS = GetDark_TdorePlayerState()
  ├── PawnExtComp->InitializeAbilitySystem(PS->ASC, PS)
  │     └── ASC->InitAbilityActorInfo(PS, Pawn)
  │           OwnerActor = PlayerState ✅
  │           AvatarActor = Character  ✅  ← 修正
  ├── PawnExtComp->HandleControllerChanged()
  │
  ▼
ADark_TdoreCharacter::SetupPlayerInputComponent(InputComponent)
  ├── PawnExtComponent->SetupPlayerInputComponent()   // InitState 通知
  └── HeroComponent->InitializePlayerInput(IC, InputConfig)
        │
        ├── NativeInputActions:
        │     ├── InputTag.Jump     → BindAction(Started/Completed, Input_Jump/End)
        │     ├── InputTag.Move     → BindAction(Triggered, Input_Move)
        │     ├── InputTag.LookMouse→ BindAction(Triggered, Input_Look)
        │     └── InputTag.Look     → BindAction(Triggered, Input_Look)
        │
        └── AbilityInputActions:
              └── IA_AbilityQ + Input.Ability.Q
                    ├── BindAction(Triggered,  Input_AbilityTagPressed,  Tag)
                    └── BindAction(Completed, Input_AbilityTagReleased, Tag)
```

---

## 三、运行时触发流程：按 Q 键

```
用户按下 Q 键
  │
  ▼
EnhancedInput 系统
  └── IMC_Default: Q → IA_AbilityQ
  │
  ▼
HeroComponent::Input_AbilityTagPressed(Input.Ability.Q)
  │
  ▼
Pawn->GetAbilitySystemComponent()  ← IAbilitySystemInterface
  └── PawnExtComp->GetDark_TdoreAbilitySystemComponent()
        └── PlayerState 上的 ASC ✅
  │
  ▼
ASC->AbilityInputTagPressed(Input.Ability.Q)
  │
  for (Spec : ActivatableAbilities.Items)
  {
      if (Spec.GetDynamicSpecSourceTags().HasTagExact(Input.Ability.Q))
          InputPressedSpecHandles.Add(Spec.Handle);  // BP_GA_TestQ 加入队列
  }
  │
  ▼
PlayerController::PostProcessInput → ASC->ProcessAbilityInput()
  │
  for (Handle : InputPressedSpecHandles)
  {
      if (Spec->Ability && !Spec->IsActive())
      {
          CancelActivationGroupAbilities();  // 互斥组先取消
          TryActivateAbility(Handle);        // ← 激活 GA_TestQ！
      }
  }
  │
  ▼
GA_TestQ::ActivateAbility()
  └── UE_LOG: "GA_TestQ 已激活！Q 键测试技能触发成功。"
```

### 按 WASD 的流程

```
WASD → EnhancedInput → IA_Move
  │
  ▼
HeroComponent::Input_Move
  │
  APawn* Pawn = GetPawn<APawn>();
  Pawn->AddMovementInput(RotateVector(FVector::RightVector), Value.X);   // A/D 左右
  Pawn->AddMovementInput(RotateVector(FVector::ForwardVector), Value.Y); // W/S 前后
  │
  ▼
UCharacterMovementComponent::AddInputVector() → CalcVelocity() → 物理移动
```

---

## 四、伤害管线：Q 键到血量减少

### 4.1 数据初始化

```
PlayerState 构造
  ├── CreateDefaultSubobject<UDark_TdoreHealthSet>   → Health=100, MaxHealth=100
  └── CreateDefaultSubobject<UDark_TdoreCombatSet>   → BaseDamage=0, BaseHeal=0

角色 ASC 初始化
  └── 应用 DefaultEffects: GE_BaseDamage_20 (infinite)
       └── Modifier: CombatSet.BaseDamage, Override, 20
            → BaseDamage.CurrentValue = 20
```

### 4.2 Q 键到 Execution 触发

```
Q 键 → EnhancedInput → HeroComponent → ASC::ProcessAbilityInput
  └── TryActivateAbility(BP_GA_TestQ)
       └── BP_GA_TestQ 蓝图: "Apply Gameplay Effect to Owner" (GE_Damage_20)
            │
            ▼
ASC::ApplyGameplayEffectToSelf(GE_Damage_20)
  │
  ├─ GE_Damage_20 配置:
  │    Duration: Instant        ← 立即执行一次
  │    Execution: Dark_TdoreDamageExecution  ← 触发自定义计算
  │
  ├─ MakeOutgoingSpec → MakeEffectContext()
  │    └── Dark_TdoreAbilitySystemGlobals::AllocGameplayEffectContext()
  │         → new FDark_TdoreGameplayEffectContext  ← 工厂保证类型
  │
  ▼
引擎: 发现 GE 有 Execution → 调用 Execute_Implementation
```

### 4.3 Execution 内部计算

```
Dark_TdoreDamageExecution::Execute_Implementation(ExecutionParams, OutExecutionOutput)
  │
  ├─ Step 1: 读攻击者 CombatSet.BaseDamage
  │    ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(
  │        BaseDamageDef,  ← 注册为 Source.CombatSet.BaseDamage, 快照=true
  │        EvaluateParameters,
  │        BaseDamage)
  │    → BaseDamage = 20  (从攻击者角色的 CombatSet 读取)
  │
  ├─ Step 2: 计算衰减 (当前均为 1.0，TODO)
  │    DistanceAttenuation         = 1.0
  │    PhysicalMaterialAttenuation = 1.0
  │    DamageInteractionMultiplier = 1.0
  │
  ├─ Step 3: 计算最终伤害
  │    DamageDone = 20 × 1.0 × 1.0 × 1.0 = 20
  │
  └─ Output:
       OutExecutionOutput.AddOutputModifier(
           HealthSet.Damage,   ← 目标: 元属性（管道值）
           Add,                ← 操作: 累加
           20)                 ← 值
```

### 4.4 AddOutputModifier 后的连锁反应

```
引擎处理 OutExecutionOutput:
  └── 修改目标角色的 HealthSet.Damage += 20
       │
       ▼
HealthSet::PostGameplayEffectExecute(Data)
  │
  ├─ Data.EvaluatedData.Attribute == Damage ?
  │    ├─ SetHealth(Health - Damage)     → Health: 100 → 80
  │    └─ SetDamage(0)                   → Damage: 20 → 0 (清零管道值)
  │
  ├─ Health 变化检测:
  │    OnHealthChanged.Broadcast(Instigator, Causer, Spec, Magnitude, OldHealth, NewHealth)
  │      → HealthComponent 监听 → UI 更新
  │
  └─ 死亡检测:
       if (Health <= 0 && !bOutOfHealth)
       {
           OnOutOfHealth.Broadcast(...)
             → HealthComponent::HandleOutOfHealth()
               → ASC::HandleGameplayEvent("GameplayEvent.Death")
                 → ASC 匹配 AbilityTriggers → 激活 GA_Death
       }
```

### 4.5 完整调用链路总结

```
配置层:
  DA_DefaultAbilitySet → BP_GA_TestQ (InputTag=Input.Ability.Q)
  GE_BaseDamage_20 → CombatSet.BaseDamage = 20
  GE_Damage_20 → Execution: Dark_TdoreDamageExecution

输入层:
  Q键 → ASC::ProcessAbilityInput → TryActivateAbility(BP_GA_TestQ)

技能层:
  BP_GA_TestQ 蓝图 → ApplyGameplayEffectToOwner(GE_Damage_20)

Execution层:
  Execute → 读取 Source.CombatSet.BaseDamage=20 → 计算=20
  AddOutputModifier → Target.HealthSet.Damage += 20

扣血层:
  PostGameplayEffectExecute → Health -= 20 → OnHealthChanged → OnOutOfHealth
```

---

## 五、新增技能流程（零 C++ 改代码）

```
1. DefaultGameplayTags.ini
   +GameplayTagList=(Tag="Input.Ability.E", DevComment="E 键技能")

2. 创建 GA 蓝图 (Content/Abilities/BP_GA_Fire)
   父类: Dark_TdoreGameplayAbility

3. DA_DefaultAbilitySet → GrantedAbilities
   加一行: Ability=BP_GA_Fire, InputTag=Input.Ability.E

4. DA_InputConfig → AbilityInputActions
   加一行: InputAction=IA_Fire, InputTag=Input.Ability.E

5. IMC_Default → 映射
   E 键 → IA_Fire
```

---

## 六、关键 API 对照表

| 环节 | 位置 | API |
|------|------|-----|
| ASC 创建 | PlayerState 构造 | `CreateDefaultSubobject<UDark_TdoreAbilitySystemComponent>()` |
| ASC 初始化 | PlayerState::PostInitializeComponents | `ASC->InitAbilityActorInfo(this, GetPawn())` |
| Avatar 修正 | Character::PossessedBy | `PawnExtComp->InitializeAbilitySystem(ASC, PS)` |
| 技能授予 | PlayerState::BeginPlay | `AbilitySet->GiveToAbilitySystem(ASC, SourceObject)` |
| 标签挂载 | AbilitySet | `Spec.GetDynamicSpecSourceTags().AddTag(InputTag)` |
| 标签匹配 | ASC | `Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag)` |
| 输入绑定 | HeroComponent::InitializePlayerInput | `BindAction(IA, TriggerEvent, this, Func)` |
| 输入路由 | PlayerController::PostProcessInput | `ASC->ProcessAbilityInput(DeltaTime, bGamePaused)` |
| 移动/视角 | HeroComponent::Input_Move/Input_Look | `Pawn->AddMovementInput()` / `Pawn->AddControllerYawInput()` |
| 属性捕获 | ExecutionCalculation 构造 | `RelevantAttributesToCapture.Add(BaseDamageDef)` |
| 伤害计算 | DamageExecution::Execute | `AttemptCalculateCapturedAttributeMagnitude(BaseDamageDef, ...)` |
| 伤害输出 | DamageExecution::Execute | `OutExecutionOutput.AddOutputModifier(Damage, Add, Value)` |
| 扣血转换 | HealthSet::PostGameplayEffectExecute | `SetHealth(Health - Damage)` |
| 死亡检测 | HealthSet::PostGameplayEffectExecute | `OnOutOfHealth.Broadcast(...)` |

---

## 七、Lyra 对齐要点

| Lyra 做法 | 我们对应 |
|-----------|---------|
| ASC 在 PlayerState | ✅ `ADark_TdorePlayerState` |
| HeroComponent 处理输入 | ✅ `UDark_TdoreHeroComponent` |
| InputConfig DataAsset | ✅ `UDark_TdoreInputConfig` |
| PawnExtComp 协调初始化 | ✅ `UDark_TdorePawnExtensionComponent` |
| PostProcessInput 处理技能 | ✅ `ADark_TdorePlayerController` |
| GetDynamicSpecSourceTags | ✅ 标签 + 匹配一致 |
| HeroComponent 直接操作 Pawn | ✅ `Pawn->AddMovementInput()` |
| DamageExecution 属性捕获 | ✅ `Dark_TdoreDamageExecution` |
| AbilitySystemGlobals 工厂 | ✅ `Dark_TdoreAbilitySystemGlobals` |
| GameplayEffectContext 扩展 | ✅ `FDark_TdoreGameplayEffectContext` |
| TagRelationshipMapping | ✅ `UDark_TdoreAbilityTagRelationshipMapping` |

---

## 八、文件路径对应

| 文件 | 作用 |
|------|------|
| `AbilitySystem/Dark_TdoreAbilitySystemComponent` | 自定义 ASC，输入路由 + 激活组管理 |
| `AbilitySystem/Dark_TdoreGameplayAbility` | GA 基类，激活策略 + 激活组 |
| `AbilitySystem/Dark_TdoreAbilitySystemGlobals` | AllocGameplayEffectContext 工厂 |
| `AbilitySystem/Dark_TdoreGameplayEffectContext` | 扩展 GE Context（CartridgeID 等） |
| `AbilitySystem/Dark_TdoreAbilityTagRelationshipMapping` | DataAsset：技能 Block/Cancel 关系表 |
| `AbilitySystem/Dark_TdoreAbilitySet` | DataAsset，捆绑技能 + 效果 |
| `AbilitySystem/Attributes/Dark_TdoreAttributeSet` | 属性集基类 |
| `AbilitySystem/Attributes/Dark_TdoreHealthSet` | 生命属性（Health, MaxHealth, Damage, Healing） |
| `AbilitySystem/Attributes/Dark_TdoreCombatSet` | 战斗属性（BaseDamage, BaseHeal） |
| `AbilitySystem/Executions/Dark_TdoreDamageExecution` | 伤害 ExecutionCalculation |
| `AbilitySystem/Executions/Dark_TdoreHealExecution` | 治疗 ExecutionCalculation |
| `AbilitySystem/Abilities/GA_TestQ` | Q 键测试技能 |
| `AbilitySystem/Abilities/GA_Death` | 死亡技能（GameplayEvent 触发） |
| `Character/Dark_TdorePawnExtensionComponent` | InitState 协调器 |
| `Character/Dark_TdoreHeroComponent` | 玩家输入处理 |
| `Character/Dark_TdoreHealthComponent` | 血量管理（监听 HealthSet） |
| `Character/Dark_TdoreCharacterMovementComponent` | 自定义移动组件 |
| `Character/Dark_TdorePawn` | AI/NPC 基类（APawn + GAS） |
| `Player/Dark_TdorePlayerState` | ASC 宿主，属性集持有者 |
| `Dark_TdorePlayerController` | PostProcessInput 处理技能 |
| `Input/Dark_TdoreInputConfig` | 输入配置 DataAsset |
| `Dark_TdoreCharacter` | 玩家角色（Camera + Movement + 组件） |
