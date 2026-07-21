# 方向调试组件 (DirectionDebugComponent) 使用指南

## 概述

`UDark_TdoreDirectionDebugComponent` 是一个调试用 ActorComponent，挂载到 Pawn/Character 上后，会在角色脚下绘制 **3 根不同颜色的方向箭头**，用于调试战斗移动和朝向逻辑。

## 三个箭头

| 箭头 | 颜色 | 标签 | 含义 | 数据来源 |
|------|------|------|------|----------|
| **Move (移动方向)** | 绿色 `FColor::Green` | "Move" | 角色实际移动输入方向 | 三级回退（见下方） |
| **Actor (面朝方向)** | 蓝色 `FColor::Blue` | "Actor" | Actor 自身的前方朝向 | `GetActorForwardVector()` |
| **Controller (控制器方向)** | 红色 `FColor::Red` | "Controller" | 控制器的 Yaw 朝向（即摄像机水平朝向） | `GetControlRotation().Yaw` → 只取 Yaw 的 Forward |

### Move 箭头的三级回退逻辑

`GetMovementDirection()` 按优先级获取方向：

1. **`CharacterMovementComponent->GetCurrentAcceleration()`** — 有加速度时用加速度方向（最优先）
2. **`GetLastMovementInputVector()`** — 无加速度时用最后一次输入方向
3. **`Velocity`** — 无输入时如果 `bUseVelocityWhenNoInput=true`，用实际速度方向（惯性滑行也会显示）

## 可调参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `bDrawDebug` | `bool` | `true` | 总开关，关闭后所有箭头隐藏 |
| `bDrawOnlyForLocallyControlledPawn` | `bool` | `true` | 只绘制本地控制的 Pawn |
| `bUseVelocityWhenNoInput` | `bool` | `true` | 无输入时是否用 Velocity 作为移动方向 |
| `bUseArrowComponents` | `bool` | `true` | `true` = 用 ArrowComponent 渲染（跟随角色）；`false` = 用 `DrawDebugDirectionalArrow` 绘制 |
| `ArrowLength` | `float` | `120.0` | 箭头长度 |
| `ArrowHeadSize` | `float` | `32.0` | 箭头头部大小 |
| `GroundOffset` | `float` | `6.0` | 箭头起点离地高度偏移 |
| `LineThickness` | `float` | `3.0` | 线条粗细 |
| `bDrawLabels` | `bool` | `true` | 是否显示文字标签（"Move" / "Actor" / "Controller"） |
| `LabelWorldSize` | `float` | `24.0` | 标签文字世界大小 |
| `MovementDirectionColor` | `FColor` | `Green` | 移动箭头颜色 |
| `ActorDirectionColor` | `FColor` | `Blue` | Actor 面朝箭头颜色 |
| `ControllerDirectionColor` | `FColor` | `Red` | 控制器方向箭头颜色 |

## 控制台命令

```
DarkTdore.Debug.DirectionArrows 0   // 关闭所有方向箭头
DarkTdore.Debug.DirectionArrows 1   // 开启所有方向箭头
```

## 箭头位置计算

- 起点：角色 **Capsule 底部中心** + `GroundOffset`（默认 6 单位离地）
- 三个箭头有不同垂直偏移避免重叠：
  - Move: `+0`
  - Actor: `+8`
  - Controller: `+16`

## 两种渲染模式

### ArrowComponent 模式（`bUseArrowComponents=true`，默认）

- 使用 `UArrowComponent` + `UTextRenderComponent` 动态创建并附加到 RootComponent
- 标签文字始终面向摄像机（Billboard 效果）
- 跟随角色移动，不需要每帧重新创建

### DrawDebug 模式（`bUseArrowComponents=false`）

- 使用 `DrawDebugDirectionalArrow` + `DrawDebugString`
- 每帧重新绘制，性能稍高但只在编辑器中可见

## 使用方式

1. 在 Pawn/Character 蓝图中添加 `Dark_TdoreDirectionDebugComponent`
2. 运行游戏即可看到三个箭头
3. 在 Details 面板调整颜色、长度等参数
4. 不需要时设置 `bDrawDebug = false` 或移除组件
