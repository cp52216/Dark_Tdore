# GAS 系统完整加载与触发流程

> 参考 Lyra Starter Game 架构
> ASC 在 PlayerState 上，输入由 HeroComponent 处理，ProcessAbilityInput 在 PlayerController::PostProcessInput

---

## 一、架构概览

```
PlayerState (Owner, 持久)
  ├── UDark_TdoreAbilitySystemComponent   ← ASC 在此，角色死亡不丢失
  ├── UDark_TdoreAttributeSet             ← 属性（血量等）在此
  └── UDark_TdoreAbilitySet               ← 通过 DataAsset 授予 GA/GE

Character (Avatar, 物理表现体)
  ├── UDark_TdorePawnExtensionComponent   ← 初始化协调器，管理 InitState
  ├── UDark_TdoreHeroComponent            ← 输入处理（参考 Lyra）
  ├── UDark_TdoreInputConfig              ← 输入配置 DataAsset
  └── Camera + MovementComponent

PlayerController
  └── PostProcessInput                    ← 调用 ASC->ProcessAbilityInput()
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

## 四、新增技能流程（零 C++ 改代码）

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

## 五、关键 API 对照表

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

---

## 六、Lyra 对齐要点

| Lyra 做法 | 我们对应 |
|-----------|---------|
| ASC 在 PlayerState | ✅ `ADark_TdorePlayerState` |
| HeroComponent 处理输入 | ✅ `UDark_TdoreHeroComponent` |
| InputConfig DataAsset | ✅ `UDark_TdoreInputConfig` |
| PawnExtComp 协调初始化 | ✅ `UDark_TdorePawnExtensionComponent` |
| PostProcessInput 处理技能 | ✅ `ADark_TdorePlayerController` |
| GetDynamicSpecSourceTags | ✅ 标签 + 匹配一致 |
| HeroComponent 直接操作 Pawn | ✅ `Pawn->AddMovementInput()` |

---

## 七、文件路径对应

| 文件 | 作用 |
|------|------|
| `AbilitySystem/Dark_TdoreAbilitySystemComponent` | 自定义 ASC，输入路由 + 激活组管理 |
| `AbilitySystem/Dark_TdoreGameplayAbility` | GA 基类，激活策略 + 激活组 |
| `AbilitySystem/Dark_TdoreAttributeSet` | 属性集（Health, MaxHealth） |
| `AbilitySystem/Dark_TdoreAbilitySet` | DataAsset，捆绑技能 + 效果 |
| `Character/Dark_TdorePawnExtensionComponent` | InitState 协调器 |
| `Character/Dark_TdoreHeroComponent` | 玩家输入处理 |
| `Character/Dark_TdoreCharacterMovementComponent` | 自定义移动组件 |
| `Character/Dark_TdorePawn` | AI/NPC 基类（APawn + GAS） |
| `Player/Dark_TdorePlayerState` | ASC 宿主 |
| `Dark_TdorePlayerController` | PostProcessInput 处理技能 |
| `Input/Dark_TdoreInputConfig` | 输入配置 DataAsset |
| `Dark_TdoreCharacter` | 玩家角色（Camera + Movement + 组件） |
