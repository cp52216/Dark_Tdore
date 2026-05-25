# 摄像机系统详解（参考 Lyra CameraComponent）

> 替代传统 SpringArm + FollowCamera，实现模式栈 + 平滑混合 + 技能覆盖视角

---

## 一、蓝图与 C++ 对应关系

| 蓝图资产 | 路径 | 继承的 C++ 类 | 在哪配置 |
|---------|------|--------------|---------|
| `CM_ThirdPerson` | `/Game/Camera/CM_ThirdPerson` | `UDark_TdoreCameraMode_ThirdPerson` | 自身 Details 面板 |
| `DA_PawnData` | `/Game/System/DA_PawnData` | `UDark_TdorePawnData` | Default Camera Mode 字段 |
| `BP_ThirdPersonCharacter` | `/Game/ThirdPerson/BP_ThirdPersonCharacter` | `ADark_TdoreCharacter` | 组件自动创建（无需手动配） |

---

## 二、关键问题解答

### Q1："在蓝图子类的 Details 面板设 FieldOfView / BlendTime / BlendFunction" 指的是哪个蓝图？

**答：指的是 `CM_ThirdPerson`（即 `BP_CameraMode_ThirdPerson`）蓝图。**

打开 `CM_ThirdPerson` → **Class Defaults**，可以看到：

```
View 分类：
  FieldOfView     = 80      ← 视野角度
  ViewPitchMin    = -89     ← 最小俯仰
  ViewPitchMax    = 89      ← 最大俯仰

Blending 分类：
  BlendTime       = 0.5     ← 混合过渡时长
  BlendFunction   = EaseOut ← 混合曲线
  BlendExponent   = 4.0     ← 曲线弯曲度
  CameraTypeTag   = (空)    ← 相机类型标签

Third Person 分类：
  DefaultCameraOffset = (-400, 0, 50) ← 默认偏移

Collision 分类：
  bPreventPenetration = true         ← 防穿墙
```

**注意**：`CM_ThirdPerson` 作为默认模式，`BlendTime` 应设为 **0**（因为初始没有可混合的旧模式），技能覆盖用的相机模式才需要 BlendTime。

### Q2：UDark_TdoreCameraMode_ThirdPerson 怎么和 UDark_TdoreCameraComponent 关联？

**答：通过三层间接关联——PawnData → HeroComponent → CameraComponent 委托 → CameraModeStack。**

```
关联链路：
  
  DA_PawnData.DefaultCameraMode = CM_ThirdPerson (TSubclassOf<UDark_TdoreCameraMode>)
         │
         ▼  HeroComponent 读取
  HeroComponent::DetermineCameraMode()
         │  返回 CM_ThirdPerson 类
         ▼  CameraComponent 的委托每帧执行
  CameraComponent::DetermineCameraModeDelegate.Execute()
         │  返回 CM_ThirdPerson 类
         ▼
  CameraComponent::UpdateCameraModes()
         │
         ▼
  CameraModeStack::PushCameraMode(CM_ThirdPerson)
         │
         ├── GetCameraModeInstance(CM_ThirdPerson)
         │      └── NewObject<UDark_TdoreCameraMode_ThirdPerson>(this)  ← 此时创建实例
         │           Outer = CameraComponent  ← 通过 GetOuter() 可反查 CameraComponent
         │
         └── CameraModeStack.Insert(CameraMode, 0)  ← 放入栈顶
                │
                ▼  每帧
         EvaluateStack(DeltaTime, OutView)
           └── UpdateStack → CameraMode->UpdateView(DeltaTime)  ← 第三人称计算视角
           └── BlendStack  → 从底向上混合
```

**核心关系：CameraMode 的 Outer 就是 CameraComponent**，通过 `GetDarkTdoreCameraComponent()` → `CastChecked<UDark_TdoreCameraComponent>(GetOuter())` 获取。

### Q3：谁调用 SetAbilityCameraMode？谁调用 DetermineCameraMode？

```
SetAbilityCameraMode:  ← GameplayAbility 在技能激活时调用
  调用者: GA（GameplayAbility）
  调用时机: ActivateAbility() 内
  目的: 临时覆盖摄像机模式（如瞄准镜、大招特写）
  流向: GA → HeroComp->SetAbilityCameraMode(AimCameraMode, SpecHandle)
         → HeroComp 存储 AbilityCameraMode
         → 下帧 DetermineCameraMode() 返回 AimCameraMode
         → CameraComponent 推入栈 → 平滑过渡到瞄准视角

ClearAbilityCameraMode:  ← GA 在技能结束时调用
  调用者: GA（GameplayAbility）
  调用时机: EndAbility() 内
  目的: 清除覆盖，恢复默认第三人称
  流向: GA → HeroComp->ClearAbilityCameraMode(SpecHandle)
         → 验证句柄匹配 → AbilityCameraMode = nullptr
         → 下帧 DetermineCameraMode() 返回 DefaultCameraMode
         → CameraComponent 推入栈 → 平滑过渡回第三人称

DetermineCameraMode:  ← 每帧由 CameraComponent 的委托执行
  调用者: CameraComponent::UpdateCameraModes()
         → DetermineCameraModeDelegate.Execute()
         → 此委托由 HeroComponent 绑定:
            CameraComp->DetermineCameraModeDelegate.BindUObject(this, &ThisClass::DetermineCameraMode)
  调用时机: 每帧 GetCameraView()
  目的: 决定当前帧应该使用哪个 CameraMode
  返回值优先级:
    1. AbilityCameraMode  （技能覆盖，非空则返回）
    2. PawnData->DefaultCameraMode  （默认第三人称）
    3. nullptr  （无模式，摄像机不工作）
```

---

## 三、完整数据流（从 PIE 启动到每帧渲染）

### 阶段 1：PIE 启动 → 组件创建

```
BP_ThirdPersonCharacter::Constructor()
  └── ADark_TdoreCharacter::Constructor()
        ├── CameraComponent = CreateDefaultSubobject<UDark_TdoreCameraComponent>("CameraComponent")
        │     └── SetupAttachment(RootComponent)
        │     └── bUsePawnControlRotation = true
        ├── PawnExtComponent = CreateDefaultSubobject<UDark_TdorePawnExtensionComponent>
        ├── HeroComponent = CreateDefaultSubobject<UDark_TdoreHeroComponent>
        └── HealthComponent = CreateDefaultSubobject<UDark_TdoreHealthComponent>
```

### 阶段 2：组件注册 → 创建 CameraModeStack

```
CameraComponent::OnRegister()
  └── if (!CameraModeStack)
        CameraModeStack = NewObject<UDark_TdoreCameraModeStack>(this)
        // Outer = CameraComponent, 所以 CameraMode 实例的 Outer 也是 CameraComponent
```

### 阶段 3：InitState 推进 → 输入绑定 → 摄像机委托绑定

```
HeroComponent::HandleChangeInitState(DataInitialized)
  └── InitializePlayerInput()
        ├── [绑定 EnhancedInput: Move/Look/Jump/Ability]
        │
        └── // ===== 摄像机委托绑定 =====
            Character->GetDarkTdoreCameraComponent()
              ->DetermineCameraModeDelegate.BindUObject(this, &ThisClass::DetermineCameraMode)
              // ↓ 绑定了回调函数：
              // TSubclassOf<UDark_TdoreCameraMode> DetermineCameraMode() const
              // {
              //     if (AbilityCameraMode) return AbilityCameraMode;      // 技能覆盖
              //     if (PawnData->DefaultCameraMode) return DefaultCameraMode; // 默认
              //     return nullptr;
              // }
```

### 阶段 4：首帧 GetCameraView → 推入默认模式

```
[引擎每帧] CameraComponent::GetCameraView(DeltaTime, DesiredView)
  │
  ├── 1. UpdateCameraModes()
  │      └── if (DetermineCameraModeDelegate.IsBound())
  │            CameraMode = Delegate.Execute()
  │              └── HeroComponent::DetermineCameraMode()
  │                    ├── AbilityCameraMode == nullptr (无技能覆盖)
  │                    └── return PawnData->DefaultCameraMode  ← CM_ThirdPerson 类
  │            │
  │            ▼
  │          CameraModeStack::PushCameraMode(CM_ThirdPerson)
  │            ├── GetCameraModeInstance(CM_ThirdPerson)
  │            │     ├── 从 CameraModeInstances 池查找 → 未找到
  │            │     └── NewObject<UDark_TdoreCameraMode_ThirdPerson>(CameraComponent)
  │            │           Outer = CameraComponent ✅
  │            │           构造函数: 初始化 7 条防穿透射线
  │            │
  │            ├── 检查是否已在栈中 → 否
  │            ├── bShouldBlend = (BlendTime>0 && StackSize>0)
  │            │     └── 因为是第一个模式 → false → BlendWeight = 1.0（立即生效）
  │            ├── CameraModeStack.Insert(CameraMode, 0)  ← 放入栈顶
  │            └── CameraMode->OnActivation()
  │
  ├── 2. CameraModeStack::EvaluateStack(DeltaTime, CameraModeView)
  │      │
  │      ├── UpdateStack(DeltaTime)
  │      │     └── for (CameraMode : Stack, 从上到下)
  │      │           CameraMode->UpdateCameraMode(DeltaTime)
  │      │             ├── UpdateView(DeltaTime)
  │      │             │     ↓ [第三人称模式]
  │      │             │     UDark_TdoreCameraMode_ThirdPerson::UpdateView()
  │      │             │       ├── UpdateForTarget() → 蹲伏偏移
  │      │             │       ├── UpdateCrouchOffset() → EaseInOut 平滑蹲伏
  │      │             │       ├── GetPivotLocation() = 胶囊体顶 + 蹲伏调整 + BaseEyeHeight
  │      │             │       ├── GetPivotRotation() = Pawn->GetViewRotation()(来自 ControlRotation)
  │      │             │       ├── Pitch = Clamp(ViewPitchMin, ViewPitchMax)
  │      │             │       ├── 偏移计算:
  │      │             │       │     └── PivotRotation.RotateVector(DefaultCameraOffset)
  │      │             │       │           = (-400, 0, 50) 旋转到世界空间
  │      │             │       │           = 角色后方 4 米, 上方 50cm
  │      │             │       ├── View.Location = Pivot + 偏移
  │      │             │       ├── View.Rotation = PivotRotation
  │      │             │       ├── View.ControlRotation = View.Rotation
  │      │             │       ├── View.FieldOfView = FieldOfView(80)
  │      │             │       │
  │      │             │       └── UpdatePreventPenetration(DeltaTime)
  │      │             │             ├── 7 条扇形 Sweep 射线从 SafeLoc → CameraLoc
  │      │             │             ├── 主射线(索引0)命中 → HardBlockedPct(立即生效)
  │      │             │             ├── 侧射线(索引1~4)命中 → SoftBlockedPct(平滑)
  │      │             │             └── DistBlockedPct 平滑插值 → CameraLoc 拉近
  │      │             │
  │      │             └── UpdateBlending(DeltaTime)  ← BlendAlpha→BlendWeight
  │      │                   └── BlendTime=0 → BlendWeight 始终 1.0
  │      │           │
  │      │           if (BlendWeight >= 1.0) → 此模式下方的旧模式可移除
  │      │
  │      └── BlendStack(CameraModeView)
  │            └── 从栈底开始（索引最后）
  │                  Base.View = 栈底模式的 View
  │                  for (向上)
  │                    Base.View.Blend(上层.View, 上层.BlendWeight)
  │                      ├── Location : Lerp
  │                      ├── Rotation : 加权旋转插值
  │                      └── FieldOfView : Lerp
  │
  ├── 3. PlayerController::SetControlRotation(View.ControlRotation)
  │      └── 保证移动方向跟随视线
  │
  ├── 4. 应用一次性 FOV 偏移 → 清零
  │
  ├── 5. SetWorldLocationAndRotation(View.Location, View.Rotation)
  │      FieldOfView = View.FieldOfView
  │
  └── 6. 输出 FMinimalViewInfo → 引擎渲染
```

### 阶段 5：GA 技能覆盖摄像机 — 完整调用链

```
[按键激活技能, 例如"瞄准"]

GA_Aim::ActivateAbility()    ← 在蓝图中
  │
  ├── // 获取 HeroComponent
  │   UDark_TdoreHeroComponent* HeroComp = UDark_TdoreHeroComponent::FindHeroComponent(Avatar);
  │
  ├── // 覆盖摄像机模式为瞄准视角
  │   HeroComp->SetAbilityCameraMode(CM_AimCamera::StaticClass(), CurrentSpecHandle);
  │   │
  │   └── HeroComponent 内部:
  │         AbilityCameraMode = CM_AimCamera;           ← 存储覆盖模式
  │         AbilityCameraModeOwningSpecHandle = Handle;  ← 存储技能句柄(用于验证)
  │
  ├── [技能逻辑...]
  │
  └── EndAbility()    ← 技能结束
        │
        └── HeroComp->ClearAbilityCameraMode(CurrentSpecHandle);
              └── if (AbilityCameraModeOwningSpecHandle == Handle)  ← 验证一致性
                    AbilityCameraMode = nullptr;                     ← 清除覆盖
                    AbilityCameraModeOwningSpecHandle = {};           ← 清除句柄

// ===== 下一帧的变化 =====

CameraComponent::GetCameraView()
  └── Delegate.Execute() → HeroComponent::DetermineCameraMode()
        ├── AbilityCameraMode = CM_AimCamera (非空!) → return CM_AimCamera  ← 返回覆盖模式
        │
        ▼
      CameraModeStack::PushCameraMode(CM_AimCamera)
        ├── bShouldBlend = (BlendTime>0 && StackSize>0)
        │     └── CM_AimCamera 的 BlendTime=0.3 → true → 初始 BlendWeight=现存贡献度
        ├── 插入栈顶: [CM_AimCamera(Weight→1.0), CM_ThirdPerson(Weight=1.0)]
        │
        ▼  0.3 秒后
      CM_AimCamera.BlendWeight = 1.0
        └── UpdateStack 检测到 BlendWeight>=1.0 → 移除 CM_ThirdPerson
        └── 栈 = [CM_AimCamera]  ← 完全切换到瞄准视角

// ===== 技能结束后的下一帧 =====

DetermineCameraMode() → AbilityCameraMode == nullptr → return CM_ThirdPerson
  └── PushCameraMode(CM_ThirdPerson)
        ├── CM_ThirdPerson.BlendTime=0 → bShouldBlend=false → BlendWeight=1.0
        └── 栈 = [CM_ThirdPerson(Weight=1.0), CM_AimCamera(Weight=持续)]
             → CM_ThirdPerson 权重到 1.0 → 移除 CM_AimCamera
             → 栈 = [CM_ThirdPerson]  ← 恢复第三人称
```

---

## 四、穿透避免详细流程

```
PreventCameraPenetration(ViewTarget, SafeLoc, CameraLoc, DeltaTime, DistBlockedPct, bSingleRayOnly)
  │
  ├── 1. 构建射线坐标系
  │     BaseRay = CameraLoc - SafeLoc
  │     BaseRayMatrix(BaseRay.Rotation())
  │     GetScaledAxes → LocalFwd, LocalRight, LocalUp
  │
  ├── 2. 遍历 7 条触角射线
  │     for (RayIdx = 0; RayIdx < 7; ++RayIdx)
  │     {
  │         Feeler = PenetrationAvoidanceFeelers[RayIdx]
  │         │
  │         if (FramesUntilNextTrace > 0) → 递减并跳过(性能优化)
  │         │
  │         // 计算带偏转角的射线方向
  │         RotatedRay = BaseRay.RotateAngleAxis(AdjustmentRot.Yaw,   LocalUp)
  │         RotatedRay = RotatedRay.RotateAngleAxis(AdjustmentRot.Pitch, LocalRight)
  │         RayTarget = SafeLoc + RotatedRay
  │         │
  │         // 球形 Sweep 检测
  │         SweepSingleByChannel(SafeLoc→RayTarget, ECC_Camera, Radius=Extent)
  │         │
  │         if (命中)
  │         {
  │             if (ActorHasTag("IgnoreCameraCollision")) → 忽略
  │             │
  │             权重 = Cast<APawn>(HitActor) ? PawnWeight : WorldWeight
  │             BlockPct = Hit.Time * (1 - (1-时间)*(1-权重))  ← 非Pawn可穿透
  │             BlockPct = ((HitLocation-SafeLoc).Size - PushOut) / (RayTarget-SafeLoc).Size
  │             │
  │             if (RayIdx == 0) → HardBlockedPct = BlockPct    ← 主射线 : 立即生效
  │             else            → SoftBlockedPct = min(SoftBlockedPct, BlockPct)  ← 侧射线 : 平滑
  │         }
  │         else
  │             FramesUntilNextTrace = TraceInterval  ← 未命中, 隔帧再检测
  │     }
  │
  ├── 3. 平滑更新 DistBlockedPct
  │     if (bResetInterpolation)
  │         DistBlockedPct = DistBlockedPctThisFrame  ← 直接快照
  │     else if (DistBlockedPct < 新值)  ← 遮挡加重
  │         DistBlockedPct += DeltaTime/PenetrationBlendOutTime * (新值 - DistBlockedPct)
  │     else                             ← 遮挡减轻
  │         DistBlockedPct -= DeltaTime/PenetrationBlendInTime * (DistBlockedPct - Soft值)
  │
  └── 4. 应用遮挡
        CameraLoc = SafeLoc + (CameraLoc - SafeLoc) * DistBlockedPct
        // DistBlockedPct = 1.0(无遮挡) → CameraLoc 不变
        // DistBlockedPct = 0.5(半遮挡) → CameraLoc 被拉近到一半距离
        // DistBlockedPct = 0.0(全遮挡) → CameraLoc = SafeLoc(贴胶囊体)
```

---

## 五、混色过渡机制详解

### BlendAlpha 推进

```
每帧 UpdateBlending(DeltaTime):
  BlendAlpha += DeltaTime / BlendTime          ← 线性递进
  BlendAlpha = Min(BlendAlpha, 1.0)            ← Clamp 上限

示例: BlendTime=0.3 秒
  帧1(0.016s): Alpha = 0.053  → 约 5% 进度
  帧5(0.080s): Alpha = 0.267  → 约 27% 进度
  帧10(0.160s): Alpha = 0.533 → 约 53% 进度
  帧19(0.304s): Alpha = 1.0   → 到达 100%
```

### BlendAlpha → BlendWeight 映射

```
Linear:    Weight = Alpha                           ← 匀速
EaseIn:    Weight = FMath::InterpEaseIn(0,1,Alpha,E)   ← 开始慢, 结束快
EaseOut:   Weight = FMath::InterpEaseOut(0,1,Alpha,E)  ← 开始快, 结束慢
EaseInOut: Weight = FMath::InterpEaseInOut(0,1,Alpha,E)← 两端平滑

EaseOut, Exponent=4.0 示例:
  Alpha=0.3 → Weight ≈ 0.76  (快速逼近)
  Alpha=0.5 → Weight ≈ 0.94
  Alpha=0.7 → Weight ≈ 0.99
  Alpha=1.0 → Weight = 1.0
```

### SetBlendWeight 逆推（PushCameraMode 时使用）

```
当 PushCameraMode 需要以"旧贡献度"作为初始权重时:
  BlendWeight = 旧贡献度 (如 0.3)
  
  需要逆推 BlendAlpha(否则下帧 BlendAlpha 从0开始不对):
  
  Linear:    Alpha = 0.3
  EaseOut:   Alpha = FMath::InterpEaseOut(0,1,0.3,1/E)
             → 这意味着 BlendAlpha 不是从0开始, 而是从"0.3 在 EaseOut 曲线上对应的 alpha" 继续
  
  然后 UpdateBlending 从这个 Alpha 继续累加 → 平滑衔接
```

---

## 六、蓝图配置速查

| 模式 | 蓝图 | 推荐配置 |
|------|------|---------|
| 默认第三人称 | `CM_ThirdPerson` | BlendTime=0, FOV=80~100, DefaultCameraOffset=(-400,0,50) |
| 瞄准特写 | 新建 `CM_Aim` | BlendTime=0.3, BlendFunction=EaseOut, FOV=50, 偏移更近 |
| 大招视角 | 新建 `CM_Ultimate` | BlendTime=0.5, BlendFunction=EaseInOut, FOV=100, 偏移更远 |

---

## 七、GA 中控制摄像机示例

```cpp
// 在 GA 的 ActivateAbility 中
void UGA_Aim::ActivateAbility(...)
{
    // 1. 获取 HeroComponent
    AActor* Avatar = GetAvatarActorFromActorInfo();
    UDark_TdoreHeroComponent* HeroComp = UDark_TdoreHeroComponent::FindHeroComponent(Avatar);
    
    // 2. 覆盖摄像机模式为瞄准视角
    HeroComp->SetAbilityCameraMode(CM_AimCamera::StaticClass(), CurrentSpecHandle);
    // → 下帧起 DetermineCameraMode → return CM_AimCamera
    // → CameraComponent 自动混合(BlendTime=0.3, EaseOut)
}

void UGA_Aim::EndAbility(...)
{
    // 3. 技能结束 → 恢复默认第三人称
    UDark_TdoreHeroComponent* HeroComp = UDark_TdoreHeroComponent::FindHeroComponent(GetAvatarActorFromActorInfo());
    HeroComp->ClearAbilityCameraMode(CurrentSpecHandle);
    // → 下帧起 DetermineCameraMode → return PawnData->DefaultCameraMode
    // → 自动混合回第三人称
}
```

---

## 八、文件清单

| 文件 | 角色 |
|------|------|
| `Camera/Dark_TdoreCameraComponent.h/cpp` | 摄像机组件，挂在 Pawn，管理模式栈 |
| `Camera/Dark_TdoreCameraMode.h/cpp` | 模式基类 + 视图容器 + 混合栈 |
| `Camera/Dark_TdoreCameraMode_ThirdPerson.h/cpp` | 第三人称实现：偏移/蹲伏/穿透避免 |
| `Camera/Dark_TdorePenetrationAvoidanceFeeler.h` | 防穿透射线数据结构 |
| `Character/Dark_TdorePawnData.h` | DefaultCameraMode 字段 |
| `Character/Dark_TdoreHeroComponent.h/cpp` | SetAbilityCameraMode / ClearAbilityCameraMode / DetermineCameraMode |
| `Dark_TdoreCharacter.h/cpp` | CameraComponent 组件声明+构造 |
