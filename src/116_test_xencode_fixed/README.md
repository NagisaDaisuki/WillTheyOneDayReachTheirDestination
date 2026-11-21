# 116_test_xencode_fixed —— 崩溃修复快照

本目录是 `../116_test_xencode/` 在**修掉 SIGSEGV 崩溃之后**的快照。
代码行为与修复版一致，只额外做了两件事：

1. 在修复处插入 `[BUGFIX-n]` 标记，说明「原来错在哪、为什么错、现在怎么改」；
2. 在仍存在的隐患处插入 `[KNOWN-ISSUE-n]` 标记（**未修复**，只标注）。

> 崩溃的 bug 不在 `XEncode` 封装层里，而在**调用方** `116_test_xencode.cpp` 的
> YUV 平面填充循环里。封装层本身是"清白的"，但它有几处设计问题（见遗留问题表）。

---

## 1. 崩溃复盘

| 项目 | 内容 |
| --- | --- |
| 现象 | `Segmentation fault (core dumped)`，EXIT=139，输出文件 0 字节 |
| 位置 | `116_test_xencode.cpp` 的 UV 填充循环 `frame->data[2][...] = ...` |
| 根因 | YUV420P 的 U/V 平面是 `width/2 × height/2`，循环却用了 `width × height` |
| 越界量 | 写到的最大偏移 1,037,759 字节 vs V 平面可用 522,304 字节 ≈ **2 倍** |

### gdb 实测证据（1920×1080）

```
(gdb) p frame->width              → 1920
(gdb) p frame->height             → 1080
(gdb) p frame->linesize[0]        → 1920
(gdb) p frame->linesize[1]        → 960      ← 只有 width/2
(gdb) p frame->linesize[2]        → 960
(gdb) p frame->buf[0]->size       → 3133568  ← 三平面共用一块 av_malloc
(gdb) p (char*)frame->data[2] - (char*)frame->data[0]                          → 2611264
(gdb) p frame->buf[0]->size - ((char*)frame->data[2]-(char*)frame->data[0])    → 522304   (V 可用)
(gdb) p ((c->height-1)*frame->linesize[2] + (c->width-1))                     → 1037759  (实际写到)
```

ASan 的原始报告：

```
ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 1 in main 116_test_xencode.cpp:124
0x... is located 0 bytes after 3133568-byte region
    allocated by av_malloc (libavutil.so.56)
```

### 修复 diff

```diff
-    for (int y = 0; y < c->height; ++y) {
-      for (int x = 0; x < c->width; ++x) {
+    for (int y = 0; y < c->height / 2; ++y) {
+      for (int x = 0; x < c->width / 2; ++x) {
         frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
         frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 2;
       }
     }
```

### 为什么之前"崩得像地址越界（Address boundary error）"

`av_frame_get_buffer()` 用 `av_malloc()`（内部 `posix_memalign`）一次性分配 Y+U+V
三平面共 3,133,568 字节。循环按亮度尺寸去写色度平面，累计越界约 515 KB，
连续的线性越界最终跨出这块 mmap 映射区，触发的是**访问不存在页**的缺页异常，
而不是踩坏别的堆对象，所以表现为干脆的段错误 / 总线错误，而非"莫名其妙的数据错误"。

> 顺带一提：U 平面其实也越界了（越界约 515 KB），但越界区域恰好落在同一块
> 缓冲区里紧邻的 V 平面，所以**不报错、只静默破坏 V 平面数据**。
> 这种"越界但不崩"的情况比崩掉更危险，只有 ASan/valgrind 能抓到。

---

## 2. 修复点索引（`[BUGFIX-n]`）

| 标记 | 文件 | 内容 |
| --- | --- | --- |
| `[BUGFIX-1]` | `116_test_xencode.cpp` UV 循环 | **主崩溃根因**：色度平面循环上界改为 `height/2`、`width/2` |
| `[BUGFIX-2]` | `116_test_xencode.cpp` 顶部常量 | 分辨率是 1920×1080 却输出 `400_300_25_*.264`，改为常量驱动文件名 |
| `[BUGFIX-3]` | `116_test_xencode.cpp` `Create()` 调用 | 静态成员函数改为 `XEncode::Create(...)` 调用，消除误导 |
| `[BUGFIX-4]` | `116_test_xencode.cpp` 收尾打印 | `count` 统计的是 packet 数，文案从 "Frame" 改为 "Packet" |
| `[CLEANUP]` | `CMakeLists.txt` | 默认 Debug（`-g`）、去掉未用的 Qt6、只链真正用到的库、RPATH 改 `DT_RPATH`、开 `-Wall -Wextra` |

---

## 3. 遗留问题索引（`[KNOWN-ISSUE-n]`，均未修复）

| 标记 | 位置 | 问题 | 严重性 |
| --- | --- | --- | --- |
| `[KNOWN-ISSUE-1]` | `xencode.h` `~XEncode` | 析构不加锁，与 `set_c`/`Encode` 并发时 UAF；接口却声称"线程安全" | 中 |
| `[KNOWN-ISSUE-2]` | `xencode.cpp` `End()` | 不幂等、`avcodec_send_frame` 返回值不检查、无"已 flush"状态机 | 中 |
| `[KNOWN-ISSUE-3]` | `xencode.cpp` `Encode()` | `send_frame` 返回 `EAGAIN` 时不重试 → **静默丢帧**，调用方无法感知 | 高 |
| `[KNOWN-ISSUE-4]` | `116_test_xencode.cpp` 主循环 | `av_frame_make_writable()` 返回值未检查 | 低 |
| `[KNOWN-ISSUE-5]` | `xencode.cpp` `Create()` | 不设 `width/height` 要求调用方填 → 隐式时序契约；`thread_count=16` 写死 | 中 |
| `[KNOWN-ISSUE-6]` | `xencode.h` `Create()` | 因前向声明，`AVCodecID` 只能退化成 `int`；返回裸指针 + `set_c` 两步走易泄漏 | 中 |
| `[KNOWN-ISSUE-7]` | `xencode.h` `set_c()` | 裸指针所有权转移、可重复 set 同一指针导致 double free、无拷贝控制 | 中 |

这些问题的完整重构方案见 `../116_test_xencode_modern/`。

---

## 4. 构建与运行

```bash
cd 116_test_xencode_fixed
cmake -S . -B build          # 默认就是 Debug
cmake --build build -j
cd build
./116_test_xencode           # 输出 1920_1080_25_crf_vbv.264
./116_test_xencode h265      # 输出 HEVC 裸流

# 校验
ffprobe -v error -count_frames \
  -show_entries stream=codec_name,width,height,nb_read_frames \
  1920_1080_25_crf_vbv.264
```

实测结果：

```
Total Packet Encoded: 1000
1920_1080_25_crf_vbv.264  3.8M
codec_name=h264  width=1920  height=1080  nb_read_frames=1000
```

## 5. 用 gdb 复现这个崩溃

要"重放"崩溃，把 `[BUGFIX-1]` 的两处 `/ 2` 去掉再编译，然后：

```bash
cd build
gdb ./116_test_xencode
(gdb) set pagination off
(gdb) run
# SIGSEGV
(gdb) bt                      # → 116_test_xencode.cpp:124（Debug 构建下才有行号）
(gdb) p i
(gdb) p x
(gdb) p y                     # y 会略大于 height/2
(gdb) p frame->linesize[2]    # 960，明显小于循环用的 c->width(1920)
```

想在**第一次越界的那一字节**就停下（而不是等它跑几十万次）：

```gdb
(gdb) break 116_test_xencode.cpp:116      # 进循环之前
(gdb) run
(gdb) p frame->data[2]
(gdb) watch -l *(char*)(frame->data[2] + 522304)
(gdb) continue
```
