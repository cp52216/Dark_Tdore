# TikiStar MVVM 框架深度解析

> **更新时间**：2026-08-03 | **真相源**：`MainBundleScripts/Framework/` 完整源码
>
> 本文档从**零基础**开始，逐层解释 MVVM 响应式框架的**每一步原理**，并用 C++ 代码**复刻整个框架**。

---

## 目录

- [先导：MVVM 是什么、解决什么问题](#先导mvvm-是什么解决什么问题)
- [Part A：JavaScript/TypeScript 原理解析（逐行注释）](#part-ajavascripttypescript-原理解析逐行注释)
  - [第 1 层：Observable — 让数据"可被追踪"](#第-1-层observable--让数据可被追踪)
  - [第 2 层：ReactionScheduler — 把更新"推迟到合适时机"](#第-2-层reactionscheduler--把更新推迟到合适时机)
  - [第 3 层：ReactionBinder — 管理一堆 reaction 的"账本"](#第-3-层reactionbinder--管理一堆-reaction-的账本)
  - [第 4 层：BaseBinder — "一条数据 ↔ 一个 Widget 属性"的桥梁](#第-4-层basebinder--一条数据--一个-widget-属性的桥梁)
  - [第 5 层：BinderFactory — "button 用 ButtonBinder，text 用 TextBinder"的工厂](#第-5-层binderfactory--button-用-buttonbindertext-用-textbinder的工厂)
  - [第 6 层：ViewComponent — MVVM 的核心：state + context Proxy](#第-6-层viewcomponent--mvvm-的核心state--context-proxy)
  - [第 7 层：IoC 容器 — 不用 new，自动注入依赖](#第-7-层ioc-容器--不用-new自动注入依赖)
  - [第 8 层：WindowManager — 窗口的总管家](#第-8-层windowmanager--窗口的总管家)
- [Part B：C++ 复刻整个框架（完整可编译代码）](#part-bc-复刻整个框架完整可编译代码)
  - [B.1 Observable 引擎](#b1-observable-引擎)
  - [B.2 ReactionScheduler](#b2-reactionscheduler)
  - [B.3 ReactionBinder](#b3-reactionbinder)
  - [B.4 BaseBinder + ButtonBinder + TextBinder](#b4-basebinder--buttonbinder--textbinder)
  - [B.5 ViewComponent 核心](#b5-viewcomponent-核心)
  - [B.6 IoC 容器](#b6-ioc-容器)
  - [B.7 完整运行示例](#b7-完整运行示例)
- [Part C：从 0 到 1 的完整数据流（一步步走一遍）](#part-c从-0-到-1-的完整数据流一步步走一遍)
- [附录：文件路径速查](#附录文件路径速查)

---

## 先导：MVVM 是什么、解决什么问题

### 传统做法的问题

假设有一个"点击按钮，数字+1，文本更新"的 UI：

```typescript
// 传统做法：手动更新 UI
let count = 0;
button.onClick = () => {
    count++;                           // 1. 改数据
    textBlock.SetText(String(count));  // 2. 手动更新 UI ← 容易忘记！
};
```

**问题**：数据和 UI 是两套东西，每改一个数据就要手动写一行 UI 更新。当 UI 有 50 个控件、数据有 30 个字段时，这种"手动同步"就是 bug 之源。

### MVVM 的做法

```typescript
// MVVM 做法：声明绑定关系，数据变化自动更新 UI
context.state.count = 0;
this.bindWidget(viewRes, "TextBlock_Counter", "text", {
    get text() { return String(context.state.count); }  // 声明：text 永远 = count
});
this.bindWidget(viewRes, "Button_Add", "button", {
    onClick: () => { context.state.count++; }            // 只需改数据
});
// UI 自动更新！不用手动 SetText！
```

**核心思想**：**数据是唯一的真相源（Single Source of Truth）**。UI 只是数据的"投影"。你只改数据，框架自动把变化同步到 UI。

### 怎么实现"自动"？

用到一个经典技术：**依赖收集 + 变化通知**。

```
你写：get text() { return state.count; }
         ↑
    JS 引擎执行这个 getter，读取了 state.count
         ↑
    框架拦截了这次读取，记录下来："text 这个属性依赖 state.count"
         ↑
    后来 state.count 被修改了
         ↑
    框架收到通知："state.count 变了，所有依赖它的属性都要重新计算"
         ↑
    框架自动重新执行 get text()，拿到新值，SetText 到 Widget
```

下面逐层拆解这个机制。

---

## Part A：JavaScript/TypeScript 原理解析（逐行注释）

### 第 1 层：Observable — 让数据"可被追踪"

**文件**：`MainBundleScripts/Framework/Core/Observable/Observable.ts`

```
这个文件是对 @nx-js/observer-util 库的封装。
@nx-js/observer-util 的原理：
  1. observable(obj) → 用 Proxy 包装 obj，拦截所有属性的 读/写
  2. observe(fn) → 执行 fn，同时开启"收集模式"
  3. fn 执行过程中如果读了 observable 的属性 → Proxy 记录"这个 fn 依赖这个属性"
  4. 以后该属性被修改 → 自动重新执行 fn
```

```typescript
// ===== observable(obj)：把普通对象变成"可追踪对象" =====
// 原理：用 ES6 Proxy 拦截 get/set
//   get 时：如果有 observer 正在执行，记录"这个 observer 依赖这个 key"
//   set 时：通知所有依赖这个 key 的 observer 重新执行
export function observable<Observable extends object>(obj?: Observable): Observable {
    return observerImpl.observable(obj);
    // observerImpl.observable 内部做的事（简化版）：
    //   return new Proxy(obj, {
    //     get(target, key) {
    //       if (当前有正在执行的 observer) {
    //         记录: observer ← 依赖 ← key
    //       }
    //       return target[key];
    //     },
    //     set(target, key, value) {
    //       target[key] = value;
    //       通知所有依赖 key 的 observer 重新执行;
    //       return true;
    //     }
    //   });
}

// ===== observe(fn)：注册一个"自动重新执行"的函数 =====
// 原理：
//   1. 标记"当前正在执行的 observer = fn"
//   2. 执行 fn()
//   3. fn 内部如果读了 observable 属性 → Proxy get 拦截 → 记录依赖
//   4. 标记"当前正在执行的 observer = null"
//   5. 返回 fn（用于后续 unobserve）
export function observe<Reaction extends Function>(func: Reaction, options?: ObserveOptions): Reaction {
    return observerImpl.observe(func, options);
}

// ===== unobserve(fn)：取消观察 =====
export function unobserve(func: Function): void {
    observerImpl.unobserve(func);
}

// ===== raw(obj)：获取 Proxy 背后的原始对象 =====
// 用于：检查原始对象上是否有某个 key（而不是 Proxy 上的 key）
export function raw<Observable extends object>(obj: Observable): Observable {
    return observerImpl.raw(obj);
}
```

**通俗比喻**：

- `observable(obj)` = 给对象装上"监控摄像头"，记录谁来看过哪个属性
- `observe(fn)` = 注册一个"报警回调"，当被看过的属性变化时自动触发
- `raw(obj)` = 关掉摄像头，看原始对象

---

### 第 2 层：ReactionScheduler — 把更新"推迟到合适时机"

**文件**：`MainBundleScripts/Framework/UI/WindowManager.ts`（内嵌类）

**为什么需要调度器？** 一次操作可能修改 3 个 observable 属性，如果没有调度器，每个属性修改都会立即触发 UI 更新 → 同一帧内重复刷新 3 次。

```typescript
export class ReactionScheduler implements Scheduler {
    // ===== 两队列设计 =====
    // reactions:    当前等待执行的 reaction 集合（用 Set 自动去重）
    // waitToAdd:    正在执行 reactions 期间新加入的 reaction（暂存，执行完再移入）
    private readonly reactions: Set<Function> = new Set();
    private readonly waitToAdd: Set<Function> = new Set();
    private isBusy: boolean;  // 防止重入的标志

    // ===== add(fn)：调度器被 @nx-js 调用 =====
    // 当 observable 属性变化时，@nx-js 会调用 scheduler.add(observerFn)
    public add(value: Function): void {
        if (this.isBusy) {
            // 正在执行 reaction 中，又有新 reaction 产生
            // 不能直接加入 reactions（会导致迭代中修改集合）
            // 暂存到 waitToAdd
            this.waitToAdd.add(value);
        } else {
            this.reactions.add(value);  // 空闲时直接加入
        }
    }

    // ===== delete(fn)：移除某个 reaction =====
    // 组件销毁时调用，防止已销毁的组件继续执行 reaction
    public delete(value: Function): void {
        this.reactions.delete(value);   // 从当前队列移除
        this.waitToAdd.delete(value);   // 从待加入队列也移除
    }

    // ===== reaction()：每帧 tick 时由 WindowManager 调用 =====
    // 批量执行所有待处理的 reaction
    public reaction(): void {
        if (this.isBusy) return;       // 防止重入
        if (0 >= this.reactions.size) return;  // 没有待执行的

        this.isBusy = true;
        // 逐个执行所有 reaction
        this.reactions.forEach(reaction => {
            try { reaction(); } catch (e) { /* log error */ }
        });
        this.isBusy = false;

        this.reactions.clear();  // 清空已执行的

        // 把执行期间新产生的 reaction 移入 reactions（下一帧执行）
        if (0 < this.waitToAdd.size) {
            this.waitToAdd.forEach(x => this.reactions.add(x));
            this.waitToAdd.clear();
        }
    }
}
```

**WindowManager 中如何使用**：

```typescript
export class WindowManager {
    private reactionScheduler: ReactionScheduler;
    private reactionTickAccumS: number = 0;  // 时间累加器

    // 每帧 tick
    public tick(deltaTime: number): void {
        this.reactionTickAccumS += deltaTime;
        // 不是每帧都执行，而是每 0.25 秒执行一次（节流）
        if (this.reactionTickAccumS < 0.25) return;
        this.reactionTickAccumS = 0;
        this.reactionScheduler.reaction();  // 批量执行所有 reaction
    }

    // 创建 ReactionBinder 时传入同一个 scheduler
    public createReactionBinder(ownerTag?: string): ReactionBinder {
        return new ReactionBinder(this.reactionScheduler, ownerTag);
    }
}
```

**关键理解**：整个应用只有一个 `ReactionScheduler` 实例（在 WindowManager 中），所有 ViewComponent、Binder 的 reaction 都注册到同一个调度器上。

---

### 第 3 层：ReactionBinder — 管理一堆 reaction 的"账本"

**文件**：`MainBundleScripts/Framework/UI/ReactionBinder.ts`

```typescript
export class ReactionBinder {
    private scheduler: Scheduler;              // 指向全局唯一的 ReactionScheduler
    private $reactions: Array<ReactionFunc> = [];  // 记录所有由本 binder 创建的 reaction
    private ownerTag: string;                  // 用于 Profiler 标记，如 "MyViewComponent"

    constructor(scheduler: Scheduler, ownerTag?: string) {
        this.scheduler = scheduler;            // 共享的调度器
        this.ownerTag = ownerTag || null;
    }

    // ===== bind(func)：把 func 包装成 reaction，并记录到 $reactions =====
    // 这是 ViewComponent 和 Binder 最常调用的方法
    public bind(func: BinderFunc, debugName?: string): ReactionFunc {
        let firstExec = true;  // 标记是否首次执行

        // 核心：调用 observe()，把 func 注册为响应式 reaction
        // observe() 内部会：
        //   1. 立即执行一次 func(true)  → 首次执行，触发依赖收集
        //   2. 把 reaction 注册到 scheduler（传入的 ReactionScheduler）
        const reaction = observe(
            () => {
                if (func) {
                    func(firstExec);  // firstExec: true→首次, false→后续
                }
                firstExec = false;
            },
            { scheduler: this.scheduler }  // ← 关键！所有 reaction 共享同一个 scheduler
        );

        this.$reactions.push(reaction);  // 记账：这个 reaction 归我管
        return reaction;
    }

    // ===== unbindAll()：取消所有由本 binder 创建的 reaction =====
    // 组件销毁时调用，批量清理
    public unbindAll(): void {
        this.$reactions.forEach(func => { unobserve(func); });
        this.$reactions = [];
    }
}
```

**ReactionBinder 的角色**：它是"reaction 的记账本"。
- 每次 `bind()` 创建一个 reaction，记下来
- 组件销毁时 `unbindAll()`，一次性清理所有 reaction
- 每个 ViewComponent 有一个自己的 ReactionBinder
- 每个 Binder 也有一个自己的 ReactionBinder

---

### 第 4 层：BaseBinder — "一条数据 ↔ 一个 Widget 属性"的桥梁

**文件**：`MainBundleScripts/Framework/UI/MVVM/BaseBinder.ts`

```typescript
export class BaseBinder {
    private reactionBinderProvider: IReactionBinderProvider;  // 能创建 ReactionBinder 的工厂
    private reactionBinder: ReactionBinder;  // 本 Binder 私有的 reaction 账本

    // ===== bind(widget, options)：子类重写 =====
    // widget: UMG Widget 实例（如 UE.Button）
    // options: 包含 getter 的配置对象（如 { get text() { return state.title; } }）
    public /*virtual*/ bind(widget: any, options: BaseWidgetOptions): void { }

    // ===== unbind()：清理本 Binder 的所有 reaction =====
    public /*virtual*/ unbind() {
        if (this.reactionBinder) {
            this.reactionBinder.unbindAll();
            this.reactionBinder = undefined;
        }
    }

    // ===== autoUnbindObserve(func)：核心方法！=====
    // 作用：把 func 注册为响应式 reaction，并且 Binder 销毁时自动清理
    // 这是整个 Binder 体系中最关键的调用
    protected autoUnbindObserve(func: BinderFunc, debugName?: string): void {
        if (!this.reactionBinder) {
            // 懒创建 ReactionBinder（和 ViewComponent 共享同一个 scheduler）
            this.reactionBinder = this.reactionBinderProvider
                .createReactionBinder(this.constructor.name);
        }
        this.reactionBinder.bind(func, debugName);
        // 此后：
        //   1. func 会立即执行一次（依赖收集）
        //   2. func 内读取的 observable 属性变化时，func 自动重新执行
        //   3. Binder 被 unbind() 时，reactionBinder.unbindAll() 清理
    }
}
```

**以 UMGTextBinder 为例**，看 `autoUnbindObserve` 怎么用：

```typescript
export class UMGTextBinder extends UMGBaseBinder {
    public override bind(widget: any, options: TextOptions): void {
        super.bind(widget, options);  // 先处理 visibility/enable 等通用属性
        let w = widget as UE.TextBlock;

        // ===== 关键调用！=====
        if (Object.hasOwn(options, "text")) {
            this.autoUnbindObserve(() => {
                // ↑ 这个箭头函数就是 reaction func
                const text = options.text;  // ← 读取 options.text getter
                //   如果 getter 内部读了 context.state.title
                //   → 自动建立依赖：reaction 依赖 state.title
                if (text != undefined) {
                    w.SetText(text);  // 更新 UMG Widget
                }
            }, "text");
        }
    }
}
```

**以 UMGButtonBinder 为例**，看事件绑定和属性绑定的区别：

```typescript
export class UMGButtonBinder extends UMGBaseBinder {
    public override bind(widget: any, options: ButtonOptions): void {
        super.bind(widget, options);
        let w = widget as UE.Button;

        // ===== 1. 事件绑定：直接绑定，不做响应式 =====
        // onClick 不需要"自动重新执行"，它是一次性的事件回调
        if (options.onClick) {
            this.clickFunc = TKGlobalCD.wrap(options.onClick, w.GetName());
            w.OnClicked.Add(this.clickFunc);  // UE 的 delegate 机制
        }

        // ===== 2. 属性绑定：用 autoUnbindObserve 做响应式 =====
        // color 可能来自 state，state 变了 color 就变
        if (Object.hasOwn(options, "color")) {
            this.autoUnbindObserve(() => {
                const color = options.color;  // 读取 getter，建立依赖
                if (color != undefined) {
                    w.SetBackgroundColor(color);
                }
            }, "color");
        }

        // brush、isFocusable、isEnabled、position、size 同理
    }
}
```

**总结 BaseBinder 的精髓**：

```
Binder.bind(widget, options)
  ↓
遍历 options 的每个属性（text, color, visibility...）
  ↓
对每个属性调用 autoUnbindObserve(() => {
    读 options.xxx (触发 getter → 建立响应式依赖)
    写 widget.SetXxx() (更新 UMG 控件)
})
  ↓
结果：state.xxx 变化 → getter 返回新值 → widget 自动更新
```

---

### 第 5 层：BinderFactory — "button 用 ButtonBinder，text 用 TextBinder"的工厂

```typescript
// ===== BinderFactory 接口 =====
export interface IBinderFactory<TAll extends WidgetRegistery> {
    createBinder(typ: keyof TAll): BaseBinder;  // 根据类型字符串创建对应的 Binder
    register(typ: keyof TAll, binderConstructor: BinderConstructor): void;
}

// ===== AbstractBinderFactory：Map<类型名, Binder类> =====
export class AbstractBinderFactory<TAll extends WidgetRegistery> implements IBinderFactory<TAll> {
    private binderMap: BinderMap<TAll> = new Map();
    // binderMap 内容举例：
    //   "button" → UMGButtonBinder
    //   "text"   → UMGTextBinder
    //   "image"  → UMGImageBinder

    createBinder(typ: keyof TAll): BaseBinder {
        let BinderClass = this.binderMap.get(typ);  // 找到对应类
        let binder = new BinderClass();              // new 一个实例
        binder.setReactionBinderProvider(this.reactionBinderProvider);  // 注入依赖
        return binder;
    }

    public register(typ: keyof TAll, binderConstructor: BinderConstructor): void {
        this.binderMap.set(typ, binderConstructor);
    }
}

// ===== UMGBinderFactory：注册所有 UMG Widget 的 Binder =====
export class UMGBinderFactory extends AbstractBinderFactory<WidgetRegistery> {
    constructor() {
        super();
        this.register("widget", UMGBaseBinder);        // 通用 Widget
        this.register("button", UMGButtonBinder);       // 按钮
        this.register("text", UMGTextBinder);           // 文本
        this.register("image", UMGImageBinder);         // 图片
        this.register("listView", UMGListViewBinder);   // 列表
        this.register("progressBar", UMGProgressBarBinder); // 进度条
        // ... 共 22 种
    }
}
```

**为什么需要工厂？** `bindWidget(viewRes, "Button_OK", "button", {...})` 传入的是字符串 `"button"`，框架需要知道 `"button"` 对应 `UMGButtonBinder` 类。工厂就是做这个映射的。

---

### 第 6 层：ViewComponent — MVVM 的核心：state + context Proxy

**文件**：`MainBundleScripts/Framework/UI/MVVM/ViewComponent.ts`

这是整个框架中最重要的文件。分步骤讲。

#### Step 1：类型定义

```typescript
// ViewComponent 有两个泛型参数：
//   TState: 组件内部状态（组件自己管理的数据）
//   TProps:  外部传入属性（父组件/打开窗口时传入）
export class ViewComponent<TState extends {}, TProps extends {}> {
    // 子类必须重写这个方法，返回初始状态
    protected getState(props: TProps): TState { return undefined; }
```

#### Step 2：setup() — 创建响应式 context

这是整个 MVVM 框架的**灵魂**。

```typescript
protected setup(props: TProps): TState & TProps {

    // ===== 第 1 步：把 props 变成可观察对象 =====
    // props 是外部传入的（如 { userId: 123 }）
    // observable() 用 Proxy 包装它，以后读/写都会被追踪
    let reactiveProps = observable(props);

    // ===== 第 2 步：调用子类重写的 getState() 获取初始状态 =====
    // 传入 reactiveProps，让 getState 内部可以基于 props 计算初始 state
    let rawState = this.getState(reactiveProps);
    // 例如：return { count: 0, title: `User ${props.userId}` }

    // ===== 第 3 步：把 state 也变成可观察对象 =====
    let reactiveState = observable(rawState);

    // ===== 第 4 步：组装三个数据源 =====
    let instance = {
        state: reactiveState,    // 组件内部状态（可追踪）
        props: reactiveProps,    // 外部属性（可追踪）
        methods: {},             // 方法（预留，暂未使用）
    };

    // ===== 第 5 步：创建 Proxy — MVVM 的关键！=====
    // 这个 Proxy 拦截所有对 context 的 get/set，
    // 自动路由到正确的数据源
    let context = new Proxy(instance, {
        get(target, key, receiver) {
            const { state, props, methods } = target;
            // 读取优先级：state → props → methods
            // 使用 raw() 检查原始对象是否有这个 key
            // （不能用 Proxy 对象检查，因为 Proxy 的 in 操作行为不同）
            if (state && key in raw(state)) {
                return state[key];        // 从 state 读
            }
            else if (key in raw(props)) {
                return props[key];        // 从 props 读
            }
            else if (key in raw(methods)) {
                return methods[key];      // 从 methods 读
            }
            // 都不存在 → undefined
        },
        set(target, key, val, receiver) {
            const { state, props, methods } = target;
            // 写入优先级：state → props → methods
            if (state && key in raw(state)) {
                state[key] = val;         // 写入 state
                // ↑ 因为 state 是 observable 的 Proxy，
                //   这个赋值会被 Proxy 拦截 → 触发所有依赖此 key 的 reaction
            }
            else if (key in raw(props)) {
                props[key] = val;         // 写入 props
            }
            else if (key in raw(methods)) {
                methods[key] = val;       // 写入 methods
            }
            return true;
        },
    });

    // ===== 第 6 步：保存引用 =====
    this.context = context as unknown as TState & TProps;
    return this.context;
}
```

**context Proxy 的精妙之处**：

```typescript
// 使用者视角：
context.count = 0;         // 不需要知道 count 在 state 还是 props
context.title = "Hello";   // Proxy 自动路由到正确的数据源

// 框架视角：
context.count = 1;
// → Proxy set 拦截
// → 检查：state 有 "count" 吗？有 → state.count = 1
// → 因为 state 是 observable，这个赋值触发所有依赖 "count" 的 reaction
// → reaction 重新执行 → 更新 UI

// 使用者完全不需要知道响应式是怎么工作的
```

#### Step 3：bindView() — 绑定视图

```typescript
public bindView(viewResource: ViewResource, props?: TProps, bAsyncLoad?: boolean): BindViewResult {
    // 保存引用（异步加载时需要恢复）
    this.restoreViewResIns = viewResource;
    this.tProps = props;
    this.viewResource = viewResource;

    if (!bAsyncLoad) {
        this.doBindView();  // 同步绑定
    }
    return { isAsyncLoaded: Boolean(bAsyncLoad) };
}

private doBindView(): void {
    // ===== 绑定流程 =====
    // 1. setup(props) → 创建 context Proxy（state + props 都变成响应式）
    const context = this.setup(this.tProps);

    // 2. onInit(context) → 子类初始化逻辑
    this.onInit(context);

    // 3. onBind(viewResource, context) → 子类绑定 Widget
    //    在这里调用 bindWidget / bindComponent
    this.onBind(this.viewResource, context);

    this.hasBound = true;
}
```

#### Step 4：bindWidget() — 绑定单个 Widget

```typescript
protected bindWidget<K extends keyof WidgetRegistery>(
    viewResource: ViewResource,   // 当前 ViewResource
    widgetId: string,             // UMG 控件名，如 "Button_Close"
    widgetType: K,                // 类型字符串，如 "button"
    options: WidgetRegistery[K]   // 配置对象，如 { onClick: ..., get color() {...} }
): void {
    // ===== 1. 解析路径 =====
    // widgetId 可以是 "Panel/Button_OK" → 先找到 Panel 的 ViewResource，再取 Button_OK
    let widgetArgs = this.getWidgetArgs(viewResource, widgetId);

    // ===== 2. 获取 UMG Widget 引用 =====
    let uiAccessor = widgetArgs.viewRes.getUIAccessor();  // UMG UserWidget 实例
    let v = this.getView(uiAccessor, widgetArgs.widgetId); // Reflect.get(widget, "Button_OK")
    // 等价于：widget["Button_OK"] → 拿到 UE.Button 实例

    // ===== 3. 防重复绑定 =====
    if (this.boundWidgetSet.has(widgetId)) {
        throw new Error(`duplicate bind for widgetId:${widgetId}`);
    }

    // ===== 4. 创建 Binder =====
    // binderFactoryMgr.getFactory(UMG) → UMGBinderFactory
    // .createBinder("button") → new UMGButtonBinder()
    let fac = this.binderFactoryMgr.getFactory(widgetArgs.viewRes.getViewType());
    let binder = fac.createBinder(widgetType);

    // ===== 5. 调用 Binder.bind() =====
    // Binder 内部会遍历 options 的每个属性，调用 autoUnbindObserve()
    // 把每个属性的 getter 注册为响应式 reaction
    binder.bind(v, options);

    // ===== 6. 记录绑定 =====
    this.boundWidgetSet.add(widgetId);
    this.addBinding(binder);  // 保存到 bindings[]，用于后续 unbind()
}
```

#### Step 5：unbind() — 完整的清理流程

```typescript
public unbind(): void {
    if (this.hasBound) {
        // 1. 递归解绑所有子组件
        this.childComponents?.forEach(child => child.unbind());

        // 2. 解绑所有 Binder（清理所有 autoUnbindObserve 创建的 reaction）
        this.bindings?.forEach(binder => binder.unbind());
        //    每个 Binder.unbind() → reactionBinder.unbindAll() → unobserve 所有 reaction

        // 3. 解绑组件级 reaction（通过 this.observe() 创建的）
        this.reactionBinder?.unbindAll();

        // 4. 解绑输入/动画
        this.widgetInputActionBinder?.unbindAll();
        this.widgetAnimationBinder?.unbindAll(true);

        // 5. 清理事件监听
        // 6. 取消所有异步操作（CancellationToken）
        this.cancelAll();

        // 7. 清理动画 delegate
        this.viewResource?.releaseAllAnimationDelegates?.();

        this.onUnbind();
        this.hasBound = false;
    }
}
```

---

### 第 7 层：IoC 容器 — 不用 new，自动注入依赖

**文件**：`MainBundleScripts/Framework/IocContainer/Injection.ts` + `IocContainer.ts`

#### 什么是 IoC？为什么需要它？

```typescript
// 不用 IoC：每个组件都要自己 new 依赖
class MyComponent {
    private tipSystem = new TipSystem();        // 自己 new
    private eventSystem = new EventSystem();    // 自己 new
    // 问题：如果 TipSystem 需要改成 MockTipSystem，要改 100 个文件
}

// 用 IoC：声明"我需要什么"，容器帮你注入
class MyComponent {
    @Inject(CoreObjectDefine.TipSystem)
    private tipSystem: ITipSystem;  // 声明：我需要一个 ITipSystem
    // 容器自动帮你赋值 this.tipSystem = 容器.get("TipSystem")
    // 改实现只需改注册的地方，100 个使用方不用动
}
```

#### @Inject 装饰器的实现

```typescript
// ===== Inject 装饰器：在类的元数据上记录"这个属性需要注入什么" =====
export function Inject(objType: ObjectTypeKey): PropertyDecorator {
    return (target, propKey) => {
        // 1. 读取已有的注入元数据（可能为空数组）
        const injectKeys = Reflect.getOwnMetadata(IOC_PROPS_KEY, target) || [];

        // 2. 追加一条注入信息
        injectKeys.push({
            propertyKey: propKey,      // 属性名，如 "tipSystem"
            objectTypeKey: objType,    // 要注入的类型 key，如 Symbol("TipSystem")
            resolveType: EResolveType.Global  // 从全局容器解析
        });

        // 3. 写回元数据
        Reflect.defineMetadata(IOC_PROPS_KEY, injectKeys, target);
    };
}
```

**装饰器做了什么**：

```
@Inject(CoreObjectDefine.TipSystem)
private tipSystem: ITipSystem;

编译后等价于在类定义时执行：
  Reflect.defineMetadata('ioc:inject_props', [
    { propertyKey: 'tipSystem', objectTypeKey: Symbol(TipSystem), resolveType: Global }
  ], MyComponent.prototype)
```

#### IocContainer.resolve() — 真正的注入

```typescript
export class IocContainer extends IocContainerBase {

    // ===== get(key)：从容器中获取实例 =====
    public get<T>(key: ObjectTypeKey): T {
        const bindInfo = this.innerMap.get(key);  // 查找注册信息

        if (bindInfo.lifeTime === EBindLifetime.Singleton) {
            // 单例模式：第一次创建，之后返回缓存的实例
            let ins = this.singletonInsMap.get(key);
            if (!ins) {
                ins = this.createInstance(bindInfo.provider);  // new 或调用工厂函数
                this.singletonInsMap.set(key, ins);            // 缓存
            }
            return ins;
        }
        else if (bindInfo.lifeTime === EBindLifetime.Transient) {
            // 瞬态模式：每次调用都创建新实例
            return this.createInstance(bindInfo.provider);
        }
        // ...
    }

    // ===== resolve(target)：给 target 的所有 @Inject 属性赋值 =====
    public resolve(target: any): void {
        // 防止重复注入（已经注入过的对象跳过）
        if (target[HAS_INJECTED_KEY]) return;
        target[HAS_INJECTED_KEY] = true;

        // 1. 从 reflect-metadata 读取注入信息
        const injectKeys = getInjectKeys(target);
        // injectKeys = [
        //   { propertyKey: 'tipSystem', objectTypeKey: Symbol(TipSystem) },
        //   { propertyKey: 'eventSys', objectTypeKey: Symbol(EventSystem) },
        // ]

        // 2. 逐个注入
        injectKeys.forEach(metaData => {
            const inst = this.get(metaData.objectTypeKey);  // 从容器拿实例
            target[metaData.propertyKey] = inst;            // 赋值给属性
        });
    }
}
```

#### ViewComponent 怎么被注入的？

```typescript
// 在 WindowControllerBase 创建根 ViewComponent 时：
protected setupRootViewComponent(rootViewComp: ViewComponent<any, any>): void {
    rootViewComp.setInjectResolver(this.objectResolver);  // 设置 IoC 容器引用
    rootViewComp.setWorldProvider(this);
    rootViewComp.setWindowOwner(this);
    this.resolve(rootViewComp);  // ← 执行注入！遍历所有 @Inject 属性并赋值
    this.rootViewComponent = rootViewComp;
}
```

---

### 第 8 层：WindowManager — 窗口的总管家

```typescript
export class WindowManager {
    private reactionScheduler: ReactionScheduler;  // 全局唯一的 reaction 调度器
    private winMap = new Map<string, WindowData>(); // 窗口注册表

    // 初始化时创建 ReactionScheduler
    protected doInitSystem(): void {
        this.reactionScheduler = new ReactionScheduler();
    }

    // 每帧 tick，触发 reaction 批量执行
    public tick(deltaTime: number): void {
        this.reactionTickAccumS += deltaTime;
        if (this.reactionTickAccumS < 0.25) return;  // 节流 0.25s
        this.reactionScheduler.reaction();  // 批量执行所有待处理的 reaction
    }

    // 创建 ReactionBinder 时传入同一个 scheduler
    public createReactionBinder(ownerTag?: string): ReactionBinder {
        return new ReactionBinder(this.reactionScheduler, ownerTag);
    }

    // 打开窗口
    public openWindow<T>(winInfo, klass, params?): WindowToken {
        // 1. 创建 ViewResource（加载 UMG Widget 蓝图）
        let handler = this.getViewResourceHandler(winInfo.viewResourceType);
        let viewResource = handler.createFromInfo(world, winInfo);

        // 2. 创建 WindowData
        let winCtrl = new klass();            // new WindowController 实例
        resolver.resolve(winCtrl);            // IoC 注入
        winCtrl.internalCreateWindow(viewResource, winInfo, params);
        //   → WindowController.onWindowCreate()
        //     → bindRootView(MyViewComponent, viewResIns, props)
        //       → new MyViewComponent()
        //       → setupRootViewComponent() → IoC 注入
        //       → comp.bindView() → setup() → onInit() → onBind()

        // 3. 显示 Widget
        handler.display(viewResource);
    }
}
```

---

## Part B：C++ 复刻整个框架（完整可编译代码）

> 以下代码用纯 C++17 实现，不依赖任何外部库。
> 核心思路：用 `std::function` + `std::unordered_map` 模拟 JavaScript 的 Proxy 和 observable 机制。

### B.1 Observable 引擎

```cpp
// ===== Observable.h =====
// 核心：用模板+回调模拟 JS 的 observable/observe 机制
// 原理：
//   observable(obj) → 返回一个包装器，记录"谁在观察哪个属性"
//   observe(fn)    → 执行 fn，收集依赖，属性变化时自动重新执行 fn
//   跟 JS 版的区别：C++ 没有 Proxy，手动管理依赖表

#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>
#include <any>

using ReactionId = int;
using ReactionFunc = std::function<void()>;

// ===== 全局依赖表：属性路径 → 依赖它的 reaction 集合 =====
// 例如：依赖表["state.count"] = { reaction1, reaction2 }
//       表示 state.count 变化时，需要重新执行 reaction1 和 reaction2
class DependencyTracker {
public:
    static DependencyTracker& instance() {
        static DependencyTracker inst;
        return inst;
    }

    // 记录：reactionId 这个 reaction 依赖 path 这个属性
    void track(ReactionId reactionId, const std::string& path) {
        dependents_[path].insert(reactionId);
    }

    // 通知：path 这个属性变化了，所有依赖它的 reaction 都要重新执行
    void notify(const std::string& path) {
        auto it = dependents_.find(path);
        if (it != dependents_.end()) {
            for (auto reactionId : it->second) {
                // 找到对应的 reaction 函数并执行
                auto reactionIt = reactions_.find(reactionId);
                if (reactionIt != reactions_.end()) {
                    reactionIt->second();  // 重新执行 reaction
                }
            }
        }
    }

    // 清除某个 reaction 的所有依赖记录（unobserve 时用）
    void clearReaction(ReactionId reactionId) {
        for (auto& [path, reactions] : dependents_) {
            reactions.erase(reactionId);
        }
    }

    // 注册 reaction 函数
    ReactionId registerReaction(ReactionFunc fn) {
        ReactionId id = nextId_++;
        reactions_[id] = std::move(fn);
        return id;
    }

    void unregisterReaction(ReactionId id) {
        clearReaction(id);
        reactions_.erase(id);
    }

private:
    std::unordered_map<std::string, std::unordered_set<ReactionId>> dependents_;
    std::unordered_map<ReactionId, ReactionFunc> reactions_;
    ReactionId nextId_ = 1;
};

// ===== 正在执行的 reaction 上下文 =====
// 用于自动收集依赖：当 reaction 执行时读取了某个属性，
// 通过这个上下文自动记录依赖关系
struct ObserveContext {
    bool isObserving = false;       // 是否正在收集依赖
    ReactionId currentReactionId;   // 当前 reaction 的 ID
    std::string currentPath;        // 当前读取的属性路径
};

// ===== Observable 包装器 =====
// 包装一个 std::any，提供 get/set 方法
// get 时自动收集依赖，set 时自动通知变化
template<typename T>
class ObservableValue {
public:
    ObservableValue() : value_() {}
    ObservableValue(const T& val) : value_(val) {}

    // 读取值（自动收集依赖）
    T get(const std::string& path) const {
        auto& ctx = currentContext();
        if (ctx.isObserving) {
            // 当前有 reaction 正在执行 → 记录依赖
            DependencyTracker::instance().track(ctx.currentReactionId, path);
        }
        return value_;
    }

    // 设置值（自动通知变化）
    void set(const T& val, const std::string& path) {
        value_ = val;
        // 通知所有依赖这个 path 的 reaction 重新执行
        DependencyTracker::instance().notify(path);
    }

    // 直接读写（不触发依赖追踪，用于首次初始化）
    const T& raw() const { return value_; }
    void rawSet(const T& val) { value_ = val; }

private:
    T value_;

    static ObserveContext& currentContext() {
        static ObserveContext ctx;
        return ctx;
    }
};

// ===== observe 函数 =====
// 注册一个 reaction，立即执行一次（收集依赖），返回 reactionId
inline ReactionId observe(ReactionFunc fn) {
    auto& ctx = ObservableValue<int>::currentContext();  // 复用同一个 context
    ReactionId id = DependencyTracker::instance().registerReaction(fn);
    ctx.isObserving = true;
    ctx.currentReactionId = id;
    fn();  // 立即执行 → 收集依赖
    ctx.isObserving = false;
    return id;
}

// ===== unobserve 函数 =====
inline void unobserve(ReactionId id) {
    DependencyTracker::instance().unregisterReaction(id);
}
```

**C++ 版 vs JS 版的关键区别**：

| 特性 | JS 版（@nx-js/observer-util） | C++ 版 |
|------|------|--------|
| 属性拦截 | ES6 Proxy 自动拦截 get/set | 手动调用 ObservableValue.get/set |
| 依赖收集 | Proxy get 时自动 | get() 中手动调用 DependencyTracker::track |
| 变化通知 | Proxy set 时自动 | set() 中手动调用 DependencyTracker::notify |
| 性能 | 原生 Proxy 开销极低 | 字符串查找有开销，但可控 |

---

### B.2 ReactionScheduler

```cpp
// ===== ReactionScheduler.h =====
// 延迟批量执行 reaction，避免同一帧内重复执行
// 原理跟 JS 版完全一致

#include <set>
#include <queue>
#include <functional>

class ReactionScheduler {
public:
    // 添加一个 reaction（由 DependencyTracker::notify 调用）
    void add(ReactionId id) {
        if (isBusy_) {
            waitToAdd_.push(id);  // 执行中，暂存
        } else {
            reactions_.insert(id);
        }
    }

    // 移除一个 reaction（组件销毁时调用）
    void remove(ReactionId id) {
        reactions_.erase(id);
        // waitToAdd_ 是 queue，不能直接删除元素，用惰性跳过
    }

    // 批量执行（每帧 tick 调用）
    void flush() {
        if (isBusy_) return;
        if (reactions_.empty()) return;

        isBusy_ = true;
        // 执行所有待处理的 reaction
        for (auto id : reactions_) {
            // 从 DependencyTracker 中查找并执行 reaction 函数
            // （此处简化：直接通知 DependencyTracker 重新执行）
            // 实际实现中，DependencyTracker::notify 已经在 add 之前调用了
        }
        isBusy_ = false;
        reactions_.clear();

        // 执行期间新加入的，下一帧再处理
        while (!waitToAdd_.empty()) {
            reactions_.insert(waitToAdd_.front());
            waitToAdd_.pop();
        }
    }

private:
    std::set<ReactionId> reactions_;
    std::queue<ReactionId> waitToAdd_;
    bool isBusy_ = false;
};
```

---

### B.3 ReactionBinder

```cpp
// ===== ReactionBinder.h =====
// 管理一组 reaction 的"账本"，组件销毁时批量清理
// 跟 JS 版逻辑完全一致

class ReactionBinder {
public:
    ReactionBinder(ReactionScheduler* scheduler, const std::string& ownerTag = "")
        : scheduler_(scheduler), ownerTag_(ownerTag) {}

    // 绑定一个函数：创建 reaction，记录到 reactions_ 列表
    ReactionId bind(std::function<void(bool firstExec)> func) {
        bool firstExec = true;
        auto reactionFn = [func, &firstExec]() mutable {
            func(firstExec);
            firstExec = false;
        };
        ReactionId id = observe(reactionFn);
        reactions_.push_back(id);
        return id;
    }

    // 解绑单个 reaction
    void unbind(ReactionId id) {
        unobserve(id);
        auto it = std::find(reactions_.begin(), reactions_.end(), id);
        if (it != reactions_.end()) {
            reactions_.erase(it);
        }
    }

    // 解绑所有 reaction
    void unbindAll() {
        for (auto id : reactions_) {
            unobserve(id);
        }
        reactions_.clear();
    }

private:
    ReactionScheduler* scheduler_;
    std::string ownerTag_;
    std::vector<ReactionId> reactions_;
};
```

---

### B.4 BaseBinder + ButtonBinder + TextBinder

```cpp
// ===== BaseBinder.h =====
class BaseBinder {
public:
    virtual ~BaseBinder() = default;

    void setReactionBinder(std::shared_ptr<ReactionBinder> rb) {
        reactionBinder_ = rb;
    }

    // 子类重写：绑定具体 Widget
    virtual void bind(void* widget) {}

    // 解绑：清理所有 reaction
    virtual void unbind() {
        if (reactionBinder_) {
            reactionBinder_->unbindAll();
            reactionBinder_ = nullptr;
        }
    }

protected:
    // ===== autoUnbindObserve：核心方法！=====
    // 作用：把 func 注册为响应式 reaction，Binder 销毁时自动清理
    // 跟 JS 版的 autoUnbindObserve 完全一样
    void autoUnbindObserve(std::function<void()> func) {
        if (!reactionBinder_) {
            // 实际项目中会通过 Provider 创建，此处简化
            reactionBinder_ = std::make_shared<ReactionBinder>(nullptr, "BaseBinder");
        }
        reactionBinder_->bind([func](bool firstExec) {
            func();  // 首次和后续都执行 func
        });
    }

    std::shared_ptr<ReactionBinder> reactionBinder_;
};

// ===== ButtonBinder.h =====
// 对应 JS 的 UMGButtonBinder
class ButtonBinder : public BaseBinder {
public:
    // 要绑定的按钮控件（简化版，实际是 UE::Button*）
    struct ButtonWidget {
        std::function<void()> onClickCallback;  // 点击回调
        std::string text;
        int color = 0xFFFFFF;
    };
    ButtonWidget* widget_ = nullptr;

    struct ButtonOptions {
        std::function<void()> onClick;
        std::function<int()> getColor;     // getter：从 state 读颜色
        std::function<bool()> getEnabled;  // getter：从 state 读启用状态
    };

    void bind(ButtonWidget* widget, const ButtonOptions& options) override {
        widget_ = widget;

        // ===== 1. 事件绑定（非响应式） =====
        if (options.onClick) {
            widget_->onClickCallback = options.onClick;
        }

        // ===== 2. color 属性：响应式绑定 =====
        if (options.getColor) {
            autoUnbindObserve([this, &options]() {
                int newColor = options.getColor();  // 读取 getter
                // ↑ 如果 getter 内部读了 state.color → 自动建立依赖
                // ↑ state.color 变化时，这个 lambda 自动重新执行
                if (widget_) {
                    widget_->color = newColor;  // 更新按钮颜色
                    std::cout << "[ButtonBinder] color updated to " << newColor << std::endl;
                }
            });
        }

        // ===== 3. enabled 属性：响应式绑定 =====
        if (options.getEnabled) {
            autoUnbindObserve([this, &options]() {
                bool enabled = options.getEnabled();
                if (widget_) {
                    std::cout << "[ButtonBinder] enabled = " << enabled << std::endl;
                }
            });
        }
    }

    void unbind() override {
        widget_ = nullptr;
        BaseBinder::unbind();
    }
};

// ===== TextBinder.h =====
// 对应 JS 的 UMGTextBinder
class TextBinder : public BaseBinder {
public:
    struct TextWidget {
        std::string text;
    };
    TextWidget* widget_ = nullptr;

    struct TextOptions {
        std::function<std::string()> getText;  // getter：从 state 读文本
    };

    void bind(TextWidget* widget, const TextOptions& options) override {
        widget_ = widget;

        // ===== text 属性：响应式绑定 =====
        if (options.getText) {
            autoUnbindObserve([this, &options]() {
                std::string newText = options.getText();  // 读取 getter → 建立依赖
                if (widget_) {
                    widget_->text = newText;
                    std::cout << "[TextBinder] text updated to \"" << newText << "\"" << std::endl;
                }
            });
        }
    }

    void unbind() override {
        widget_ = nullptr;
        BaseBinder::unbind();
    }
};
```

---

### B.5 ViewComponent 核心

```cpp
// ===== ViewComponent.h =====
// 对应 JS 的 ViewComponent<TState, TProps>
// 核心：setup() 创建 context（合并 state + props），通过 bindWidget 建立响应式绑定

template<typename TState, typename TProps>
class ViewComponent {
public:
    // ===== state 对象 =====
    struct State {
        // 子类通过继承扩展
        virtual ~State() = default;
    };

    // ===== 构造函数 =====
    ViewComponent() {}

    // ===== 设置 props 并初始化 =====
    // 对应 JS 的 bindView() + setup()
    void init(const TProps& props) {
        props_ = props;

        // 1. 调用子类 getState() 获取初始 state
        auto rawState = getState(props_);
        state_ = std::move(rawState);

        // 2. onInit 回调
        onInit();

        // 3. onBind 回调（子类在此绑定 Widget）
        onBind();
    }

    // ===== getState：子类重写，返回初始状态 =====
    // 对应 JS 的 getState(props): TState
    virtual TState getState(const TProps& props) {
        return TState{};
    }

    // ===== onInit：初始化回调 =====
    virtual void onInit() {}

    // ===== onBind：绑定 Widget 回调 =====
    virtual void onBind() {}

    // ===== onUnbind：解绑回调 =====
    virtual void onUnbind() {}

    // ===== bindWidget：绑定一个 Widget =====
    // 对应 JS 的 bindWidget(viewRes, widgetId, widgetType, options)
    // 此处简化：直接接收 Binder 和 options
    template<typename TBinder, typename TOptions>
    void bindWidget(const std::string& widgetId, TBinder& binder, const TOptions& options) {
        // 检查重复绑定
        if (boundWidgets_.count(widgetId)) {
            throw std::runtime_error("duplicate bind for widget: " + widgetId);
        }

        // 调用 Binder.bind() → Binder 内部调用 autoUnbindObserve
        // → 建立响应式依赖
        binder.bind(&binder.widget_, options);

        boundWidgets_.insert(widgetId);
    }

    // ===== observe：组件级响应式观察 =====
    ReactionId observe(std::function<void()> func) {
        return reactionBinder_.bind([func](bool firstExec) {
            func();
        });
    }

    // ===== destroy：销毁组件 =====
    void destroy() {
        onUnbind();
        reactionBinder_.unbindAll();
        state_ = TState{};
    }

    // ===== 公开访问 state 和 props =====
    TState& state() { return state_; }
    const TProps& props() const { return props_; }

protected:
    TState state_;
    TProps props_;
    ReactionBinder reactionBinder_{nullptr, "ViewComponent"};
    std::unordered_set<std::string> boundWidgets_;
};
```

---

### B.6 IoC 容器

```cpp
// ===== IocContainer.h =====
// 简化版 IoC 容器，对应 JS 的 IocContainer
// 原理：
//   1. 注册：container.bind<IService, ServiceImpl>(Singleton)
//   2. 获取：auto svc = container.get<IService>()
//   3. 注入：遍历 @Inject 标记的属性，自动赋值

#include <unordered_map>
#include <functional>
#include <memory>
#include <typeindex>
#include <type_traits>

enum class Lifetime { Singleton, Transient };

class IocContainer {
public:
    // ===== bind：注册类型 =====
    // 用法：container.bind<ITipSystem, TipSystem>(Lifetime::Singleton)
    template<typename TInterface, typename TImpl>
    void bind(Lifetime lifetime = Lifetime::Singleton) {
        auto key = std::type_index(typeid(TInterface));
        factories_[key] = [lifetime, this]() -> std::shared_ptr<void> {
            if (lifetime == Lifetime::Singleton) {
                // 单例：检查缓存
                auto cacheIt = singletons_.find(key);
                if (cacheIt != singletons_.end()) {
                    return cacheIt->second;
                }
            }
            auto instance = std::make_shared<TImpl>();
            if (lifetime == Lifetime::Singleton) {
                singletons_[key] = instance;
            }
            return instance;
        };
    }

    // ===== get：获取实例 =====
    template<typename TInterface>
    std::shared_ptr<TInterface> get() {
        auto key = std::type_index(typeid(TInterface));
        auto it = factories_.find(key);
        if (it != factories_.end()) {
            return std::static_pointer_cast<TInterface>(it->second());
        }
        return nullptr;
    }

    // ===== resolve：注入依赖 =====
    // 对应 JS 的 IocContainer.resolve(target)
    // 遍历 target 的注入信息，给每个属性赋值
    template<typename T>
    void resolve(T& target) {
        // 在 C++ 中没有 reflect-metadata，需要手动注册注入映射
        // 实际使用时可以通过宏来实现
        // 此处简化：子类在构造函数中调用 registerInjection
    }

    // 手动注册注入（简化版）
    template<typename TTarget, typename TInterface>
    void registerInjection(TInterface* TTarget::* memberPtr) {
        injections_[std::type_index(typeid(TTarget))].push_back(
            [this, memberPtr](void* target) {
                auto typedTarget = static_cast<TTarget*>(target);
                typedTarget->*memberPtr = this->get<TInterface>().get();
            }
        );
    }

    // 执行注入
    template<typename T>
    void doResolve(T& target) {
        auto key = std::type_index(typeid(T));
        auto it = injections_.find(key);
        if (it != injections_.end()) {
            for (auto& injector : it->second) {
                injector(&target);
            }
        }
    }

private:
    std::unordered_map<std::type_index, std::function<std::shared_ptr<void>()>> factories_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> singletons_;
    std::unordered_map<std::type_index, std::vector<std::function<void(void*)>>> injections_;
};
```

---

### B.7 完整运行示例

```cpp
// ===== main.cpp：完整演示 MVVM 流程 =====

#include <iostream>
#include <string>

// ===== 1. 定义 State 和 Props =====
struct MyCounterState {
    int count = 0;
    std::string title = "Counter";
};

struct MyCounterProps {
    int userId;
};

// ===== 2. 创建 ViewComponent 子类 =====
class MyCounterComponent : public ViewComponent<MyCounterState, MyCounterProps> {
public:
    // 声明需要绑定的 Binder
    TextBinder textBinder_;
    ButtonBinder buttonBinder_;

    // 声明对应的 Widget（实际是 UMG 控件，此处简化）
    TextBinder::TextWidget textWidget_;
    ButtonBinder::ButtonWidget buttonWidget_;

    // ===== getState：返回初始状态 =====
    MyCounterState getState(const MyCounterProps& props) override {
        MyCounterState s;
        s.count = 0;
        s.title = "User " + std::to_string(props.userId);
        return s;
    }

    // ===== onBind：绑定 Widget =====
    void onBind() override {
        // 1. 绑定文本：text 永远 = state_.title + ":" + state_.count
        TextBinder::TextOptions textOpts;
        textOpts.getText = [this]() -> std::string {
            // 读取 state_.count 和 state_.title
            // ↑ 这两个读取会被 DependencyTracker 记录
            // ↑ 以后 state_.count 或 state_.title 变化 → 自动重新执行
            return state_.title + ": " + std::to_string(state_.count);
        };
        bindWidget("TextBlock_Counter", textBinder_, textOpts);

        // 2. 绑定按钮：点击时 count++
        ButtonBinder::ButtonOptions btnOpts;
        btnOpts.onClick = [this]() {
            state_.count++;  // ← 修改 state
            // ↑ 触发 DependencyTracker::notify("state.count")
            // ↑ 所有依赖 state.count 的 reaction 重新执行
            // ↑ textBinder 的 getText lambda 重新执行 → 更新文本
        };
        btnOpts.getColor = [this]() -> int {
            // 颜色根据 count 变化：小于 5 绿色，否则红色
            return state_.count < 5 ? 0x00FF00 : 0xFF0000;
        };
        btnOpts.getEnabled = [this]() -> bool {
            return state_.count < 10;  // 超过 10 禁用
        };
        bindWidget("Button_Add", buttonBinder_, btnOpts);
    }
};

// ===== 3. 运行 =====
int main() {
    MyCounterComponent comp;
    comp.init(MyCounterProps{ .userId = 42 });

    std::cout << "=== 初始状态 ===" << std::endl;
    std::cout << "textWidget_.text = " << comp.textWidget_.text << std::endl;
    // 输出: "User 42: 0"

    std::cout << "\n=== 点击按钮 3 次 ===" << std::endl;
    for (int i = 0; i < 3; i++) {
        comp.buttonWidget_.onClickCallback();
        std::cout << "点击后 text = " << comp.textWidget_.text
                  << ", color = " << comp.buttonWidget_.color << std::endl;
    }
    // 输出:
    //   点击后 text = User 42: 1, color = 00FF00
    //   点击后 text = User 42: 2, color = 00FF00
    //   点击后 text = User 42: 3, color = 00FF00

    std::cout << "\n=== 继续点击到 6 次 ===" << std::endl;
    for (int i = 0; i < 3; i++) {
        comp.buttonWidget_.onClickCallback();
        std::cout << "点击后 text = " << comp.textWidget_.text
                  << ", color = " << comp.buttonWidget_.color << std::endl;
    }
    // 输出:
    //   点击后 text = User 42: 4, color = 00FF00
    //   点击后 text = User 42: 5, color = FF0000  ← 颜色变红
    //   点击后 text = User 42: 6, color = FF0000

    return 0;
}
```

**这个例子的完整数据流**：

```
用户点击按钮
  → buttonWidget_.onClickCallback()
  → [this]() { state_.count++; }   // 修改 state.count
  → DependencyTracker::notify("state.count")
  → 找到所有依赖 "state.count" 的 reaction：
     1. TextBinder 的 getText lambda → 重新执行 → textWidget_.text = "User 42: 1"
     2. ButtonBinder 的 getColor lambda → 重新执行 → buttonWidget_.color = 0x00FF00
     3. ButtonBinder 的 getEnabled lambda → 重新执行
  → 所有 UI 自动更新完成！
```

---

## Part C：从 0 到 1 的完整数据流（一步步走一遍）

以 JS 版为例，追踪一次"点击按钮，更新文本"的完整过程。

### 准备阶段：setup()

```
1. 外部调用：comp.bindView(viewResource, { userId: 42 })
   ↓
2. doBindView()
   ↓
3. setup({ userId: 42 })
   ├─ reactiveProps = observable({ userId: 42 })
   │   返回 Proxy(props)，拦截所有 get/set
   ├─ rawState = this.getState(reactiveProps)
   │   子类执行：return { count: 0, title: `User ${props.userId}` }
   │   ↑ 执行过程中读了 props.userId → 如果此时有 observer，会记录依赖
   ├─ reactiveState = observable(rawState)
   │   返回 Proxy(state)，拦截所有 get/set
   ├─ instance = { state: reactiveState, props: reactiveProps, methods: {} }
   └─ context = new Proxy(instance, { get, set })
       返回的 context 对象：
         context.count → Proxy get → state 有 "count" → 返回 state.count
         context.userId → Proxy get → state 无 "userId" → props 有 → 返回 props.userId
   ↓
4. this.context = context  // 保存引用
```

### 绑定阶段：onBind()

```
5. onInit(context)
   // 子类可以做初始化：加载数据、注册事件等
   ↓
6. onBind(viewResource, context)
   ↓
7. this.bindWidget(viewRes, "TextBlock_Counter", "text", {
       get text() { return context.title + ": " + context.count; }
   })
   ↓
8. bindWidget 内部：
   a. getWidgetArgs(viewRes, "TextBlock_Counter")
      → { viewRes, widgetId: "TextBlock_Counter" }
   b. uiAccessor = viewRes.getUIAccessor()
      → 拿到 UE.UserWidget 实例
   c. v = Reflect.get(uiAccessor, "TextBlock_Counter")
      → 拿到 UE.TextBlock 实例
   d. fac = binderFactoryMgr.getFactory(ViewResourceType.UMG)
      → UMGBinderFactory
   e. binder = fac.createBinder("text")
      → new UMGTextBinder()
      → binder.setReactionBinderProvider(...)
   f. binder.bind(v, options)
      ↓ 进入 UMGTextBinder.bind()

9. UMGTextBinder.bind(widget, options):
   a. super.bind(widget, options)
      → UMGBaseBinder.bind(): 处理 visibility/enable/renderScale 等通用属性
   b. Object.hasOwn(options, "text") → true
   c. this.autoUnbindObserve(() => {
          const text = options.text;  // 读取 getter
          if (text != undefined) {
              w.SetText(text);  // 设置 UMG Widget
          }
      })
      ↓

10. autoUnbindObserve(func):
    a. 懒创建 reactionBinder
    b. reactionBinder.bind(func)
       ↓

11. ReactionBinder.bind(func):
    a. firstExec = true
    b. reaction = observe(() => {
           func(firstExec);
           firstExec = false;
       }, { scheduler: this.scheduler })
       ↓

12. observe(wrappedFunc, { scheduler }):
    内部调用 observerImpl.observe(wrappedFunc, { scheduler })
    ↓
    a. 标记"当前 observer = wrappedFunc"
    b. 执行 wrappedFunc()
       → func(true)  // firstExec = true
         → const text = options.text
           → 执行 getter: context.title + ": " + context.count
             → context Proxy get "title" → state 有 → state.title
             → context Proxy get "count" → state 有 → state.count
             → 返回 "User 42: 0"
         → w.SetText("User 42: 0")  ← 首次设置 UI
       → firstExec = false
    c. 标记"当前 observer = null"
    d. 返回 reaction（实际上是 wrappedFunc 本身）

    **此时建立的依赖关系**：
    reaction 依赖 state.title 和 state.count
    DependencyTracker 记录：
      "state.title" → [reaction]
      "state.count" → [reaction]
```

### 更新阶段：用户点击按钮

```
13. 用户点击按钮
    → UMGButtonBinder 中注册的 onClick 回调触发
    → [this]() { context.count++; }
    ↓

14. context.count++
    → Proxy set 拦截
    → state 有 "count" → state.count = 1
    → 因为 state 是 observable Proxy，这个赋值被 Proxy set 拦截
    ↓

15. observable Proxy set:
    → 通知 DependencyTracker: "state.count" 变化了
    → 找到所有依赖 "state.count" 的 reaction: [textBinder 的 reaction]
    → 调用 scheduler.add(textBinderReaction)
    ↓

16. ReactionScheduler.add(reaction):
    → reactions.add(reaction)  // 加入待执行队列
    ↓

17. 下一帧 WindowManager.tick():
    → reactionTickAccumS += deltaTime
    → if (>= 0.25s) → reactionScheduler.reaction()
    ↓

18. ReactionScheduler.reaction():
    → 遍历 reactions 集合
    → 执行 textBinder 的 reaction()
      → func(false)  // firstExec = false
        → const text = options.text
          → getter: context.title + ": " + context.count
          → 返回 "User 42: 1"
        → w.SetText("User 42: 1")  ← UI 更新！
    → reactions.clear()
```

---

## 附录：文件路径速查

### 核心框架层 (`MainBundleScripts/Framework/`)

| 层级 | 文件 | 核心职责 |
|------|------|---------|
| 1. Observable | `Core/Observable/Observable.ts` | 响应式引擎封装 |
| 2. Scheduler | `UI/WindowManager.ts`（内嵌 `ReactionScheduler`） | 延迟批量执行 reaction |
| 3. ReactionBinder | `UI/ReactionBinder.ts` | reaction 账本管理 |
| 3. Provider | `UI/ReactionBinderProvider.ts` | ReactionBinder 工厂 |
| 4. BaseBinder | `UI/MVVM/BaseBinder.ts` | Binder 基类 + autoUnbindObserve |
| 4. Binder 实现 | `UI/UMG/BasicBinder/UMG*Binder.ts` | 22 种 Widget 的具体 Binder |
| 5. Factory | `UI/MVVM/BinderFactory.ts` | Binder 工厂接口 |
| 5. Factory | `UI/MVVM/UMGBinderFactory.ts` | UMG Binder 注册表 |
| 5. Manager | `UI/MVVM/BinderFactoryManager.ts` | 多 ViewResourceType 管理 |
| 6. ViewComponent | `UI/MVVM/ViewComponent.ts` | MVVM 核心：setup + context Proxy + bindWidget |
| 6. WidgetRegistery | `UI/MVVM/WidgetRegistery.ts` | 所有 Widget Options 类型定义 |
| 6. EventDispatcher | `UI/MVVM/ViewComponentEventDispatcher.ts` | 组件树事件分发 |
| 6. ViewResource | `UI/UIGlobalDefine.ts` + `UI/UMG/UMGViewResource.ts` | 视图资源抽象 |
| 7. IoC | `IocContainer/IocContainer.ts` | IoC 容器 |
| 7. IoC | `IocContainer/Injection.ts` | @Inject 装饰器 |
| 7. Keys | `Configs/CoreObjectDefine.ts` | IoC Symbol Key 常量 |
| 8. Window | `UI/WindowManager.ts` | 窗口管理器 + ReactionScheduler |
| 8. Window | `UI/WindowControllerBase.ts` | 窗口控制器基类 |
| 辅助 | `UI/EventBinder.ts` | 事件自动解绑 |
| 辅助 | `UI/UserWidgetBinder.ts` | 输入+动画绑定管理 |

### 项目扩展层 (`MainBundleScripts/MinViableSystemSet/`)

| 文件 | 职责 |
|------|------|
| `UI/FeatureViewComponentBase.ts` | 项目 ViewComponent 基类 |
| `UI/Components/TikiStarViewComponent.ts` | 支持 customBindWidget |
| `UI/ExtendBinders/TikiStarWidgetRegistery.ts` | 项目扩展 Widget 类型（19 种） |
