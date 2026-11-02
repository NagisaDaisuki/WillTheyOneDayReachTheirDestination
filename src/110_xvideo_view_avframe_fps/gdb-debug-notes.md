---
title: 用 GDB 调试 Qt/SDL 渲染项目的两个问题：双重 sleep 与控件被盖住
publishedAt: 2026-10-31
description: 以 Qt+SDL 的 YUV 播放器为例，演示如何用 GDB 断点、单步、观察点定位逻辑 bug，以及用排除法 + xwininfo 定位平台窗口层问题。
tags: [AI, GDB, 调试, Qt, SDL, Linux, C++]
category: 调试笔记
draft: false
---

这两个问题恰好代表了 GDB 的两类典型用法：

- **问题 2（逻辑 bug）**：GDB 能直接抓到。
- **问题 1（平台/窗口层问题）**：GDB 只能帮你"排除法定位"。

## 背景回顾

| 问题 | 现象 | 根因 |
|---|---|---|
| 1. FPS label / SpinBox 不显示 | Windows 正常，Linux 下被 SDL 画面盖住 | 控件是 alien widget，被原生 X11 窗口（SDL 绘制的 label）整体覆盖 |
| 2. sleep 体感变慢 | 25fps 实际只有 ~12.5fps | `Main()` 里 `MSleep(1000/cur_fps_)` 之后又无条件 `MSleep(40)`，双重睡眠 |

## 准备工作

```bash
# 确保 Debug 编译（有 -g 没有 -O2，否则单步会跳来跳去、变量显示 <optimized out>）
cmake -DCMAKE_BUILD_TYPE=Debug .. && make

cd build
gdb ./110_xvideo_view_avframe_fps
(gdb) set pagination off   # 关掉翻页打断（可选）
(gdb) r                    # 运行
```text

## 问题 2：双重 sleep —— GDB 能直接抓到

### 方法 1：断点命令列表，直接"看见"每轮循环睡了两次

```text
(gdb) b MSleep
(gdb) commands
Type commands for breakpoint 1, one per line:
> silent
> printf "[tid %d] MSleep(%lu ms)\n", $_thread, ms
> continue
> end
(gdb) c
```text

输出会是这样：

```text
[tid 2] MSleep(40 ms)
[tid 2] MSleep(40 ms)
[tid 2] MSleep(40 ms)
[tid 2] MSleep(40 ms)   ← 每轮循环命中两次，双重睡眠直接现形
```text

配合秒表看：两次打印 + 绘制时间 ≈ 80ms/帧。这就是"体感慢了"的证据链。

### 方法 2：单步走一遍循环

```text
(gdb) b SdlPlayYuv::Main
(gdb) c
(gdb) n        # 走到 ViewS()
(gdb) n        # if (cur_fps_ > 0)
(gdb) n        # 走进 MSleep(1000/cur_fps_)，出来后
(gdb) n        # ← 你会发现下一行还是 MSleep(40)，没有 else
```text

`next`/`n` 逐行走一遍，控制流一目了然。这也是最常见的调试直觉训练：**怀疑哪段逻辑，就单步走哪段**。

### 方法 3：观察点（watchpoint），顺便学习跨线程调试

```text
(gdb) watch cur_fps_      # 数据被谁改了就断在哪
(gdb) c
Hardware watchpoint 2: cur_fps_
Old value = 25
New value = 60
#0  SdlPlayYuv::View() at sdlplayyuv.cpp:107   ← 改动来自 View()（GUI 线程）
(gdb) info threads     # 此时能看到渲染线程停在别处
```text

watch 会在**修改发生的那一行**停下来并显示线程切换，正好能体会"`cur_fps_` 被两个线程访问"这件事——数据竞争就是这么被发现的。

## 问题 1：控件不显示 —— GDB 排除法 + X11 工具配合

这类问题的思路不是"GDB 找到答案"，而是**用 GDB 证明逻辑层没问题，把嫌疑范围缩到窗口层**。

### 第 1 步：证明代码在正常执行

```text
(gdb) b SdlPlayYuv::View
(gdb) c
(gdb) p view_fps_                # 非 0x0，控件对象存在
$1 = std::unique_ptr<QLabel> = {get() = 0x5555...}
(gdb) p view_fps_->isVisible()   # Qt 认为它可见！
$2 = true
(gdb) p set_fps_->value()
$3 = 25
(gdb) p view_fps_->x() @ view_fps_->y()   # 位置也对
```text

关键结论：**对象存在、text 在更新、Qt 自己认为它 visible** —— 但屏幕上看不到。逻辑层排除，问题必然在绘制/窗口合成层。

### 第 2 步：GDB + xwininfo 交叉验证窗口层级

```text
(gdb) p (unsigned long)ui->label->winId()
$4 = 0x3a00002
(gdb) p (unsigned long)view_fps_->winId()   # alien widget → 返回的是顶层窗口 id，没有自己的 X 窗口
$5 = 0x3a00001
```text

然后在另一个终端：

```bash
xwininfo -tree -root | grep -A5 0x3a00001
```text

你会看到 SDL 画的 label 是一个**独立的 X 子窗口**，而 `view_fps_` 没有——它只是被"画"进顶层窗口的 backing store 里，层级天然低于 label 的原生 X 窗口，于是被 SDL 每帧覆盖。这就是 alien widget 问题的实锤。

> **注意一个坑**：在 GDB 里调用 `winId()` 本身会把 widget 提升为原生窗口（副作用），所以验证完要重启程序，别被这个副作用骗了。

### 顺便验证修复后的效果

改完代码（加了 `WA_NativeWindow`）再跑一次：

```text
(gdb) p (unsigned long)view_fps_->winId()
$6 = 0x3a0007d    ← 现在有自己的 X 窗口 id 了，和 label 平级且 z-order 更高
```text

## 常用命令速查表

| 命令 | 作用 | 本例中的用法 |
|---|---|---|
| `b 函数/文件:行` | 断点 | `b MSleep`、`b SdlPlayYuv::View` |
| `b MSleep if ms > 40` | 条件断点 | 只在长睡眠时停下 |
| `n` / `s` | 单步（跳过/进入函数） | 走 `Main()` 循环看控制流 |
| `c` | 继续运行 | — |
| `p expr` | 打印表达式 | `p cur_fps_`、`p view_fps_->isVisible()` |
| `watch var` | 观察点，数据变化即断 | 抓 `cur_fps_` 被谁改 |
| `bt` | 调用栈 | 停下时看"我是怎么到这的" |
| `info threads` / `thread N` | 多线程 | 区分渲染线程和 GUI 线程 |
| `commands ... end` | 断点触发时自动执行 | 打日志不中断（方法 1） |
| `finish` | 执行到当前函数返回 | 验证 `Init()` 返回值 |

## 方法论总结

- **逻辑 bug**（问题 2）：断点 + 单步 + watch 直接定位。
- **界面/平台类 bug**（问题 1）：用 GDB 排除逻辑层，然后换窗口层工具（`xwininfo`、`xtrace`）继续追。

调试器告诉你"代码没写错"，反而比找到 bug 更有价值——因为它把搜索空间砍掉了一半。
