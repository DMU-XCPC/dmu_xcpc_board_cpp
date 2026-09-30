# `execution` 入门指南

面向初学者的 `std::execution`（P2300，sender/receiver 模型）介绍，结合本项目
（`framework/execution`）的封装与用法。

本项目的执行层直接使用 NVIDIA 的参考实现 `stdexec`（vendored 在
`third_party/stdexec`），并把常用名字重导出为 `fw::` 别名。读完本文你会明白：
**sender 是什么、怎么组合、怎么运行、怎么取消、怎么和协程配合**。

> 命名空间速记：`stdexec::` 是标准/拟标准部分；`exec::` 是扩展部分
> （`exec = experimental::execution`）。本项目的别名都在 `fw::`。

---

## 1. 为什么需要它

C++ 旧的异步手段（`std::async`/future、回调、裸线程）都有问题：难以安全组合、
无法表达"在哪个线程/上下文执行"、常有动态分配与共享所有权、生态间不兼容。

sender/receiver 模型把异步计算拆成清晰的部分：

- **统一模型**：计算、I/O、网络、GPU 都能表达。
- **可组合**：提供通用算法（`then`、`when_all`…）拼装异步流程。
- **结构化并发与取消**：错误与取消沿管道传播。
- **协程友好**：可以在协程里 `co_await` 一个 sender，也可以把 awaitable 当 sender。
- **零开销组合**：编译期搭好管道，运行期不额外分配、不引用计数。
- **可定制**：自己的 scheduler、allocator、stop token 都能接进来。

一句话：**sender 用来终结回调地狱。**

---

## 2. 四个核心概念

| 概念 | 一句话 | 角色 |
|---|---|---|
| **Scheduler** | 轻量的"在哪儿执行"的句柄 | 线程池 / 事件循环 / GPU |
| **Sender** | 惰性的异步计算描述（"将来会产出值"） | 生产者 |
| **Receiver** | 计算完成时的回调集合 | 消费者 |
| **Operation State** | 一次"连接"后的具体运行实体 | `connect` 的产物 |

### Sender：惰性的计算描述
一个 sender **本身什么都不做**。它只描述"当被连接并启动时，会异步地产生值/错误/停止"。
重复使用同一个 sender（只要可复制）会得到多次独立计算。

### Receiver：完成通道
完成有三种，恰好发生一种：

- `set_value(rcvr, vs...)`：成功，携带若干值。
- `set_error(rcvr, err)`：失败，携带一个错误（如 `std::exception_ptr`）。
- `set_stopped(rcvr)`：被取消（"停止"），不携带值。

你写的类型只要暴露 `using receiver_concept = stdexec::receiver_tag;` 并提供相应
`set_*` 成员，就满足 receiver 概念。**绝大多数情况下你不用手写 receiver**，
算法会在内部生成。

### connect + start：从描述到运行
```
operation_state = stdexec::connect(sender, receiver);   // 搭建
stdexec::start(operation_state);                         // 启动
```
`connect` 返回 operation state（真正的运行体）；`start` 启动它。完成后 receiver 的
某个 `set_*` 会被调用。操作状态必须活得比计算久。

### Scheduler：在哪儿执行
```
auto sched = ...;                       // 某个调度器
auto s = stdexec::schedule(sched);      // 立刻在该上下文完成 set_value()
```
把 `schedule(sched)` 接到管道头部，后续工作就跑在该上下文上。

---

## 3. 完成信号（completion signatures）

每个 sender 会"声明"自己可能的完成方式，称为 **completion signatures**，例如：

```
stdexec::completion_signatures<
    stdexec::set_value_t(int),
    stdexec::set_error_t(std::exception_ptr),
    stdexec::set_stopped_t()>
```

编译器据此检查管道是否合法（`sender_in<S, Env>`、`sender_to<S, R>`），并在类型层面
做错误传播。初学者不必手写它，但要理解它为什么让"组合"变得类型安全。

---

## 4. 组合：工厂 / 适配器 / 消费者

### 4.1 工厂（产生 sender）
| 名称 | 作用 |
|---|---|
| `stdexec::schedule(sched)` | 在 `sched` 上开始，完成空值 |
| `stdexec::just(vs...)` | 立即以给定值完成（同步注入） |
| `stdexec::just_error(e)` | 立即以错误完成（常用于测试） |
| `stdexec::just_stopped()` | 立即以停止完成 |

### 4.2 适配器（拿到 sender，产出新 sender）
| 名称 | 作用 |
|---|---|
| `then(f)` | 值到来时调用 `f`，转发其返回值 |
| `let_value(f)` | 值到来时调用 `f`，`f` 返回一个 **sender** 并接入管道 |
| `upon_error(f)` | 出错时用 `f` 恢复为值 |
| `upon_stopped(f)` | 被取消时用 `f` 恢复为值 |
| `let_error(f)` / `let_stopped(f)` | 同上，但恢复步骤本身是异步（返回 sender） |
| `when_all(s1, s2, ...)` | 并发跑多个，全部完成后汇聚成一个元组 |
| `starts_on(sched, snd)` | 让 `snd` 从 `sched` 开始（整个管道留在那） |
| `continues_on(snd, sched)` | 跑完 `snd` 后**切换**到 `sched` |
| `on(sched, snd)` | 去 `sched` 转一圈再回到原上下文 |
| `into_variant(snd)` | 把多种值完成合并为 `std::variant` |

适配器既可以用函数式调用，也可以用管道：

```cpp
auto s = stdexec::just(21)
       | stdexec::then([](int x) { return x * 2; })
       | stdexec::then([](int x) { return x + 1; });
// s 完成时值为 43
```

**`then` vs `let_value`**：`then` 的处理函数返回**值**；`let_value` 返回**sender**。
把一个"返回 sender 的函数"交给 `then` 几乎总是 bug（你会得到一个"值是 sender"的
sender）。

### 4.3 消费者（把 sender 跑起来）
| 名称 | 作用 |
|---|---|
| `stdexec::sync_wait(snd)` | 阻塞当前线程，返回 `std::optional<std::tuple<...>>` |
| `stdexec::sync_wait_with_variant(snd)` | 多种值完成时用 |
| `exec::start_detached(snd)` | 即发即忘（要求 sender **不会** `set_error`） |
| `stdexec::spawn` / `exec::async_scope` | 把任务挂到一个可 join 的作用域 |

`sync_wait` 的语义：
- 成功 → `optional` 有值（元组）；
- 错误 → **抛出**（`exception_ptr` 会重抛，`error_code` 包成 `system_error`）；
- 停止 → `optional` 为空。

> `sync_wait` 会阻塞，**只用于顶层**（`main`、测试、叶子工具），不要在线程池/管道
> 中间调用。管道中间要"等一下"请用 `let_value` 或协程 `co_await`。

---

## 5. 惰性：先描述，后运行

```cpp
auto s = stdexec::just(42) | stdexec::then([](int x) { return x * 2; });
// 到这里为止：什么都没发生
auto r = stdexec::sync_wait(std::move(s));  // 现在才运行
```
`sync_wait` 会 `connect` + `start` 并驱动内部 `run_loop` 直到完成。

### 5.1 能否重复发射？（multi-shot vs single-shot）

**区分两个层级**：

- **operation state**（`connect` 的产物）永远是**一次性**的：`start` 一次，之后重新
  `connect` 才能再跑。对同一个 opstate `start` 两次是未定义行为。
- **sender** 能否被多次 `connect`，则取决于类型：
  - **可重用（multi-shot）**：`connect` 不要求 `&&`、且 sender 可复制，例如
    `stdexec::just(v)`、`stdexec::schedule(sched)`、以及对可复制 sender 组合出的
    `then` / `when_all`。每次 `connect` 得到独立 opstate，可重复、甚至并发连接。
  - **单次（single-shot）**：`connect` 是 `&&` 限定 / sender 只可移动，例如
    **`stdexec::task<T>`（协程）**、`exec::any_sender`。它们被 `connect`/启动即被消耗。

**想复用一个"单次" sender**（或多个消费者共享一次计算结果）用：

- `exec::split(sndr)`：**急切启动一次**、缓存结果，返回一个**可多次连接的 multi-shot
  sender**（`split` 的注释即 "multi-shot sender"）。适合"一份计算、多个订阅者"。
- `exec::ensure_started(sndr)`：急切启动，返回一个 awaitable（供消费）。
- `exec::fork_join`：多消费者场景。

**本项目里的对应关系**：
- `fw::task<T>`（我们的 handler 返回类型）= **单次**。`Server` 每次请求 `dispatch` 并
  `co_await` 一次，正好用一次。
- `fw::just` / `fw::schedule` = 可重用。
- `fw::net::async_sleep(...)` 每次调用都新建定时器；对同一个返回值重复连接会复用同一
  定时器，所以**按单次使用**——需要多次就再调用一次工厂，或用 `exec::split` 共享。

一句话：**sender 可不可重复用看类型；opstate 一定是一次性的。**

### 5.2 一个 sender 只完成一次；多次事件用 sequence sender

`sender + receiver` 建模的是**一次**异步操作：`connect` 之后，receiver **恰好收到一次**
完成信号（`set_value` 或 `set_error` 或 `set_stopped` 之一）。所以**普通 sender 不是
"会多次触发的连接点"**，不能用它表示一个反复发生的事件源。

**receiver 同理**：一个 receiver 实例在**一次 operation 里只应被完成一次**。想接多个
"消费者"就给每次 `connect` 各自一个 receiver（或其可复制副本），而不是对同一个 receiver
反复 `set_value`。

要表达**多值/多次事件流**（async generator），用 **sequence sender**：

- 概念/标签：`exec::sequence_sender_tag`；协议：`exec::set_next(receiver, item)` 返回一个
  "next-sender"，receiver 会**多次**收到 `set_next(item)`（项），最后收到一次终止完成
  （`set_value`/`set_stopped`）。
- 现成算法：`exec::sequence(...)`、`exec::repeat_n(...)`、`exec::repeat_until(...)`、
  `exec/sequence/iterate.hpp`、`merge`/`transform_each`/`any_sequence_of` 等。
- 或者老老实实用**显式队列/通道**，每个事件构成一次新的单次操作（这正是本项目里
  "每个 accept / 每次 read 都是一次新的 connect+start" 的做法）。

注意区分：
- `exec::split`：把**一次**完成的结果**广播**给多个消费者（multi-shot，但仍一次完成）。
- sequence sender：**一次连接、多个值**（真正的多次事件）。

### 5.3 组合后的 operation state 不是"一次性跑完"

- `connect` 把整棵 sender 组合成**一个** operation state（内部是嵌套的 opstate 树），它在
  整个管道期间存活；`start` 只启动它**一次**。
- `start` 返回时**不保证**已完成。异步 sender 会挂起，之后在自己的调度器上恢复，因此
  管道会分**多轮**执行，甚至跨**不同线程/上下文**。
- 只有全部由**同步** sender（`just`/`then`/`upon_*`）组成的管道，才会在 `start` 的调用栈
  内**同步跑完**。
- 一旦包含异步 sender（`schedule`、定时器、socket I/O），就会挂起；`continues_on`/`on`
  还会中途切换执行上下文。
- 管道内各阶段默认**顺序**执行；要并发用 `when_all`，`bulk` 可并行循环。
- 完成只发生一次；错误/取消会**短路**后续阶段。

```cpp
starts_on(io, async_read) | then(parse) | continues_on(cpu) | then(compute);
```
从 `io` 开始；`async_read` 挂起 → 在 `io` 上恢复 → `parse` 在 `io` → 切到 `cpu` → `compute`
在 `cpu`。`start` 早已返回。**一次 connect、一次 start、一次完成，但跨多轮、多线程。**

---

## 6. 环境与查询（了解即可）

receiver 带一个 **environment**，算法可通过 **query** 读取上下文信息：

- `get_scheduler` / `get_start_scheduler`：当前调度器；
- `get_stop_token`：取消令牌；
- `get_allocator` / `get_frame_allocator`：分配器。

`read_env(query)` 把某个查询变成 sender 的值；`write_env(sender, props...)` 给子管道
注入环境。初学阶段知道有这回事即可，用到时再查。

---

## 7. 取消：stop token

取消是协作式的。核心类型（本项目在 `fw/execution/stop.hpp` 重导出为 `fw::`）：

```cpp
fw::inplace_stop_source source;
fw::inplace_stop_token token = source.get_token();

token.stop_requested();      // 是否已被请求取消
token.stop_possible();       // 是否可能被取消
source.request_stop();       // 请求取消（会触发所有回调）
```

- 在 sender 内部用 `get_stop_token()` 拿到当前 receiver 环境的 token，并据此提前退出。
- `when_all` 在任一子任务失败时会对其兄弟**请求停止**，因此能快速收敛。
- 当被 await 的 sender 完成 `set_stopped` 时，协程会被取消（不再恢复，连同其调用链被销毁）。

---

## 8. 与协程集成

这是本项目最喜欢的部分：**sender 和协程是一套模型的两面**。

- **sender 可在协程里 `co_await`**：条件是协程的 promise 参与 "awaitable-sender
  protocol"（例如 `stdexec::task`），且 sender 只有一种值完成方式。
- **awaitable 可当 sender**：任何 awaitable 都能参与 `then`/`when_all` 等算法。

```cpp
stdexec::task<int> compute(int x) {
    int y = co_await stdexec::just(x * 2);   // 在 task 里 await 一个 sender
    co_return y + 1;
}

int main() {
    auto [v] = stdexec::sync_wait(compute(20)).value();  // v == 41
}
```

语义：
- await 的 sender 报错 → 协程**抛异常**；
- await 的 sender 停止 → 协程被**取消**（不再恢复）。

`task<T>` 本身既是协程返回类型，**又是 sender**：所以 `sync_wait(task)`、把 task 交给
`then`/`when_all`、在 task 里 await 别的 sender，全都成立。

> 本项目 `fw::task<T>` = `stdexec::task<T>`；另有扩展 `exec::task`。

---

## 9. 一个完整的小例子

用线程池并行算三个平方，再在结果上做一步：

```cpp
#include <stdexec/execution.hpp>
#include <exec/static_thread_pool.hpp>
#include <cstdio>

namespace ex = stdexec;

int main() {
    exec::static_thread_pool pool{4};
    auto sched = pool.get_scheduler();

    auto work = ex::when_all(
        ex::starts_on(sched, ex::just(0) | ex::then([](int i) { return i * i; })),
        ex::starts_on(sched, ex::just(1) | ex::then([](int i) { return i * i; })),
        ex::starts_on(sched, ex::just(2) | ex::then([](int i) { return i * i; })));

    auto [a, b, c] = ex::sync_wait(std::move(work)).value();
    std::printf("%d %d %d\n", a, b, c);   // 0 1 4
}
```

要点：`starts_on(sched, ...)` 让每个分支在池上跑；`when_all` 并发汇聚；`sync_wait`
在 `main` 里运行整条管道。

---

## 10. 在本项目（`fw::`）里怎么用

框架把常用名字重导出，业务代码不必直接写 `stdexec::`：

```cpp
#include "fw/execution/task.hpp"
#include "fw/net/io.hpp"

// 协程任务 + 直接在 task 里 await 网络 I/O 的 sender
fw::task<void> demo(fw::net::io_context& io) {
    co_await fw::net::async_sleep(io, std::chrono::milliseconds(5));
    int v = co_await fw::just(42);                     // 同步注入
    co_return;
}

int main() {
    fw::net::io_context io{2};
    auto r = fw::sync_wait(demo(io));                  // 顶层运行
    (void)r;
}
```

- `fw::task<T>`、`fw::sync_wait`、`fw::just`、`fw::then`、`fw::let_value`、
  `fw::when_all`、`fw::starts_on`、`fw::continues_on`、`fw::on`、`fw::let_error`、
  `fw::upon_error` 都是 `stdexec::` 的别名。
- I/O 层（`fw::net`）把 Asio 的异步操作桥接成 sender，业务侧统一 `co_await`。
- 服务端 `HandlerResult` 支持"同步直接返回 `http::Response`"或"返回 `fw::task<Response>`"
  两种 handler，同步路径零分配。
- 取消令牌见 `fw/execution/stop.hpp`。

### 10.1 让主线程成为执行资源（`run_loop`）

`fw::run_loop` 是一个**手动驱动、线程安全**的任务队列：其它线程把任务投递到它的
`get_scheduler()`，拥有它的线程调用 `run()` 阻塞处理，直到有人调用 `finish()`。
它让主线程也成为一名执行者，工作线程可以把"属于主线程"的任务（信号、ui、关闭）
推过来。

`Server` **自己持有一个 `run_loop`**（并通过 `main_scheduler()` 暴露其调度器），
所以应用层不需要手动管理主循环。信号是**进程级**职责，框架不放进 `Server`，由应用
用 `fw::net::on_signal` 自行安装：

```cpp
fw::net::io_context io{2};
fw::server::Server server{io, {host, port}};
register_routes(server.router());

server.start();     // 只发射工作线程，立即返回（逃逸仓）
fw::net::on_signal(io, {SIGINT, SIGTERM}, [&server] { server.stop(); });
server.run();       // 主线程驱动服务器的 run_loop，直到 stop()
server.wait();      // 排空在途连接
```

要点：
- **`server.start()` 不阻塞**：默认只启动工作线程，控制权仍在主线程手里——这就是"逃逸仓"。
- `server.run()` 让主线程驱动 `Server` 内部的主线程队列；`stop()` 会结束它。
- **信号不属于 `Server`**：进程级职责应由应用持有（`fw::net::on_signal` / 自定义
  `fw::net::signal_waiter`），避免多实例/测试里互相抢信号。
- 工作线程要让主线程做事，投递到 `server.main_scheduler()`：
  `fw::starts_on(server.main_scheduler(), fw::just(fn))`，或 `exec::execute(sched, fn)`。
- `run_loop::run()`/`finish()` 可跨线程调用（内部是原子队列）。
- 你也可以在 `Server` 之外单独使用 `fw::run_loop`（例如给非服务端程序一个主线程队列）。


---

## 11. 常见陷阱

1. **忘记消费**：只构造 sender 不 `sync_wait`/`start_detached`/`spawn`，就什么也不会发生。
2. **在管道中间 `sync_wait`**：会死锁/阻塞执行线程。中间等待用 `let_value` 或 `co_await`。
3. **`then` 返回了 sender**：应该用 `let_value`。
4. **`start_detached` 的 sender 会出错**：它要求没有 `set_error` 完成；先用
   `upon_error`/`let_error` 处理掉。
5. **运行期性能错觉**：组合是编译期的，但等待完成仍需有人运行；`sync_wait` 会阻塞
   调用线程。
6. **多次值完成**：`sync_wait`/单值 `co_await` 要求恰好一种 `set_value` 形状；多种形状
   用 `sync_wait_with_variant` / `into_variant`。
7. **生命周期**：operation state 与 sender 内的资源必须活到计算完成；协程里用局部变量
   持有即可，但 `let_value` 捕获的值会一直活到内层 sender 完成（本项目 `async_sleep`
   正是靠这一点保住定时器）。

---

## 12. 术语表

| 术语 | 含义 |
|---|---|
| Scheduler | "在哪儿执行"的轻量句柄 |
| Sender | 惰性的异步计算描述 |
| Receiver | 完成回调集合（`set_value/error/stopped`） |
| Operation state | `connect` 产生的可 `start` 的运行体 |
| Completion signatures | sender 声明的可能完成集合 |
| Env / Query | receiver 的环境与查询（scheduler、stop token、allocator） |
| Stop token | 协作式取消令牌 |
| `task` | 既是协程返回类型又是 sender |
| `sync_wait` | 顶层阻塞消费者 |

---

## 13. 延伸阅读

- 本地头文件：`third_party/stdexec/include/stdexec/execution.hpp`（伞头文件）、
  `exec/task.hpp`、`stdexec/stop_token.hpp`。
- 官方文档：<https://nvidia.github.io/stdexec>（User's Guide / Developer's Guide /
  Reference）。
- 标准提案：[P2300 `std::execution`](https://wg21.link/p2300)。
- 本项目设计说明：`docs/architecture.md`。
