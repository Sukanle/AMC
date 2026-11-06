# AMC 基准测试

<p align="center">
  中文 · <a href="README.md">English</a>
</p>

本节记录 AMC 的性能基线、扩展性和回归门槛。它是优化计划的证据，不是宣传页：下面所有数字都来自 Release 构建、固定 P-core 集合和 Google Benchmark JSON。

> 基线快照：Linux cache 复测为 **2026-11-07**；Pass/Artifact/Query/Multifile 与 Windows 跨平台数据为 **2026-11-06**。代码包含 C++ 前端/后端拆分、共享 CodeIn/Codegen、OPT-4/OPT-6/OPT-7 和单文件 cache 重构。

## 测什么

| 套件      | 可执行文件                         | 回答的问题                                            |
| --------- | ---------------------------------- | ----------------------------------------------------- |
| Pass      | `amc-pass-perf-bench`              | semantic/ABI 验证、lowering、兼容性、完整流水线       |
| Artifact  | `amc-artifact-perf-bench`          | `.abix` 读写、sidecar 加载成本、metadata、materialize |
| Cache     | `amc-cache-perf-bench`             | 冷写入、hit/miss、依赖失效、并发查询                  |
| Query     | `amc-query-perf-bench`             | 类型/函数查询、text/JSON 渲染、LLM context            |
| Multifile | `amc-multifile-perf-bench`         | 单进程线程池只读 Artifact load（仅对照）              |
| External  | `tools/run_multiprocess_bench.py`  | 构建系统模型：N 个独立 `amc` 进程                     |
| Stress    | `tools/stress_concurrent_build.py` | 多个 `amc build` 共享一个 cache                       |

## 方法

- Release 构建，Google Benchmark，`--benchmark_min_time=0.2s`；原始 Pass/Artifact/Query 表格使用 3 次重复，cache 复测与 Windows 对照使用 5 次。
- CPU 亲和性限制在 P-core 集合（`tools/run_bench.py --pin p`）。
- 使用 **wall time**（`real_time`）。多线程/多进程场景不使用 `cpu_time`，因为它只统计调用线程。
- 输入由 fixture 确定性生成；page cache 是热的，测的是 AMC 而不是冷磁盘 I/O。
- `ctest -L perf` 运行每个套件的 0.05s 短冒烟版本。
- 2026-11-07 的 Linux 复测中 Pass、Artifact、Query、Multifile 均在 ±3% 内，因此这些表格保留 2026-11-06 数据；只有 Cache 更新。绝对值仍会随编译器、CPU 和 governor 漂移。

复现：

```bash
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release -DAMC_BUILD_BENCHMARKS=ON
cmake --build build/Release --parallel

python tools/run_bench.py --pin p --repetitions 5 --out build/bench
python tools/run_multiprocess_bench.py \
  --artifact build/Release/Aue/generated/build/build/counter.abix \
  --instances 1 2 4 8 16
python tools/stress_concurrent_build.py \
  --config Aue/Counter.abic.toml --instances 8 --cold-rounds 2 --warm-rounds 4
```

## Pass 流水线

5k / 20k 实体，P-core，mean wall time：

| Benchmark      |       5k |       20k | 4x 输入 |
| -------------- | -------: | --------: | ------: |
| SemanticVerify | 0.724 ms |  3.158 ms |   4.36x |
| AbiVerify      | 1.260 ms |  5.581 ms |   4.43x |
| Lowering       | 1.995 ms | 10.080 ms |   5.05x |
| LoweringVerify | 2.869 ms | 13.296 ms |   4.63x |
| FullPipeline   | 4.698 ms | 21.150 ms |   4.50x |
| Compatibility  | 3.576 ms | 15.518 ms |   4.34x |

解读：

- 之前的 O(n²) 路径已经消失。4x 输入现在只需 4.3–5.1x。
- Lowering 是增长最陡的阶段（5.05x），但仍低于 `≤ 6x` 门槛。
- FullPipeline 约等于 Lowering + 验证；20k 在 P-core 上约 21 ms。

## Artifact 与 sidecar

5k / 10k 实体：

| Benchmark            |        5k |        10k | 2x 输入 |
| -------------------- | --------: | ---------: | ------: |
| WriteAbix            | 20.229 ms |  42.486 ms |   2.10x |
| ReadAbix             | 17.021 ms |  38.404 ms |   2.26x |
| LoadArtifact（full） | 91.358 ms | 197.657 ms |   2.16x |
| LoadAbiOnly          | 16.507 ms |  37.401 ms |   2.27x |
| LoadWithSemantic     | 61.275 ms | 133.081 ms |   2.17x |
| LoadWithLowering     | 55.130 ms | 108.595 ms |   1.97x |
| BuildMetadata        |  1.566 ms |   3.198 ms |   2.04x |
| VerifyMetadata       |  1.036 ms |   2.063 ms |   1.99x |
| ModuleFromRegion     |  0.764 ms |   1.561 ms |   2.04x |
| Materialize          |  0.089 ms |   0.177 ms |   1.99x |
| MaterializeRegion    |  0.853 ms |   1.725 ms |   2.02x |

5k 时 sidecar 成本：

```text
LoadAbiOnly       16.5 ms   (≈ ReadAbix 17.0 ms)
+ lowering        55.1 ms
+ semantic        61.3 ms
full load         91.4 ms
```

`LoadOptions` 允许 backend 跳过 `.semantic`；生成路径只需要 ABI + lowering。`amc build` / `amc verify` 保留完整加载，因为它们需要两个 sidecar。

## Cache

200 / 1000 条目，P-core，mean wall time。这是单文件 entry 重构和 POSIX `stat`
快速路径之后的 Linux 复测（2026-11-07）；重构前数据见[优化历史](#优化历史)。

| Benchmark         |      200 |      1000 | 5x 输入 |
| ----------------- | -------: | --------: | ------: |
| StoreCold         | 1.668 ms |  8.444 ms |   5.06x |
| LookupHit         | 1.044 ms |  5.407 ms |   5.18x |
| LookupHitShallow  | 0.723 ms |  3.796 ms |   5.25x |
| LookupMiss        | 0.290 ms |  1.437 ms |   4.96x |
| LookupConcurrent  | 0.371 ms |  1.795 ms |   4.84x |
| VerifyAll         | 0.845 ms |  4.381 ms |   5.18x |
| DependencyChanged |   5.1 µs |    5.1 µs |   1.00x |

新增 cache 用例：

| Benchmark                  |      200 |      1000 |
| -------------------------- | -------: | --------: |
| LookupHitWithDeps（8 依赖） | 2.595 ms | 23.735 ms |
| LookupHitLarge / 8（1 MiB） |        — |  6.515 ms |

1000 条目时单条目成本：`StoreCold` 8.4 µs、`LookupHit` 5.4 µs、
shallow hit 3.8 µs、miss 1.4 µs。

重构内容：per-key 目录改为单个版本化 entry 文件；发布时不再读回 payload；
lookup 直接打开 entry；64 KiB 以上 payload 使用 mmap。POSIX 上增加 `stat`
快速路径，在构造 `ifstream` 前判断 entry 是否存在；Windows 保留直接打开，
因为 `CreateFile` 元数据操作更贵。

相对重构前基线：`StoreCold` 快 45%，`VerifyAll` 快 20%，`LookupHit` 快 10%。
`LookupMiss/1000` 为 1.437 ms，重构前为 1.415 ms（+1.5%，约 0.02 µs/lookup）。
POSIX 快速路径补回了直接打开带来的 +15% 回归，代价是命中路径约 +4%
（5.407 ms vs 直接打开的 5.178 ms，但仍低于重构前的 6.029 ms）。
`LookupMissDiagnose/200` 为 52.9 ms，仍是独立的诊断路径，会扫描 manifest。

cache 写入是进程安全的：entry 先写入进程唯一临时文件，再用 rename 提交；
并发写入最终只保留一个 entry。

## Query 与 context

5k / 20k 类型和函数：

| Benchmark         |        5k |       20k | 4x 输入 |
| ----------------- | --------: | --------: | ------: |
| TypeExact         |    8.2 µs |   26.9 µs |   3.38x |
| TypeSubstring     |   26.7 µs |  102.3 µs |   3.78x |
| TypeHex           |    3.9 µs |   18.1 µs |   4.50x |
| FunctionExact     |   12.6 µs |   41.5 µs |   3.15x |
| TypesText         |  1.794 ms |  7.290 ms |   4.06x |
| TypesJson         |  2.508 ms | 10.284 ms |   4.10x |
| FunctionsText     |  1.555 ms |  6.329 ms |   4.07x |
| FunctionsJson     |  2.962 ms | 12.891 ms |   4.35x |
| ContextLlm        |  9.655 ms | 42.025 ms |   4.35x |
| ContextLlmStream  |  9.705 ms | 40.856 ms |   4.21x |
| ContextJson       | 10.828 ms | 46.660 ms |   4.31x |
| ContextJsonStream | 10.512 ms | 44.699 ms |   4.25x |

说明：

- 精确 name/ID 查询是微秒级；旧的无条件 type index 已移除。
- text/JSON/context 渲染随输出规模线性增长。
- streaming context 避免一次性物化完整文档，略快，并被 `amc context -o` 和 stdout 使用。

### 为什么不用 `llvm::json::OStream`？

我们评估过 `llvm::json::OStream`，但在这个 workload 上它约慢 3x，原因是大量短属性和临时字符串：

```text
ContextJson / 5k    10.7 ms -> 20.1 ms
TypesJson / 5k       2.5 ms ->  7.6 ms
FunctionsJson / 5k   3.0 ms ->  9.1 ms
```

最终保留手动 JSON writer，但直接写入 `llvm::raw_ostream`。string API 是薄包装，需要返回值的调用方行为不变；CLI/文件输出避免完整拷贝。

## 多文件并行

AMC **不内嵌进程池**。与 Ninja 启动多个 `clang` 一样，多文件吞吐由构建系统启动多个独立 `amc` 进程完成。这样隔离全局状态，也避免跨文件线程安全审计。

### 外部进程

`amc generate` 1–32 并发进程，5 次重复：

| instances | mean wall | files/sec | speedup |
| --------: | --------: | --------: | ------: |
|         1 |  14.62 ms |     68.41 |   1.00x |
|         2 |  14.11 ms |    141.72 |   2.07x |
|         4 |  15.14 ms |    264.21 |   3.86x |
|         8 |  14.78 ms |    541.15 |   7.91x |
|        16 |  15.53 ms |   1030.57 |  15.06x |
|        32 |  28.15 ms |   1136.68 |  16.62x |

16 进程与 P-core 数量匹配；32 进程超配，吞吐开始变平。多进程 harness 没有显式 CPU pinning；把它当作扩展性指标，而不是 P-core 绝对值。

### 共享 cache

多个 `amc build`，同一 config，不同 `-B`，同一 `--cache`：

| instances |     cold |    warm | artifact digest | cache entries |
| --------: | -------: | ------: | --------------- | ------------: |
|         8 | 21.35 ms | 7.41 ms | 全部一致        |             1 |
|        16 | 19.98 ms | 7.21 ms | 全部一致        |             1 |

每个进程使用唯一的 frontend/verify 临时路径。并发 writer 独立暂存 cache entry，最终只提交一个；所有输出 hash 一致。

### 单进程内对照

`amc-multifile-perf-bench` 也用 `llvm::DefaultThreadPool` 测量只读 artifact load：

| files | sequential | thread pool | speedup |
| ----: | ---------: | ----------: | ------: |
|     1 |   3.743 ms |    3.800 ms |   0.98x |
|     2 |   7.399 ms |    4.000 ms |   1.85x |
|     4 |  14.509 ms |    4.180 ms |   3.47x |
|     8 |  28.577 ms |    6.441 ms |   4.44x |
|    16 |  58.329 ms |   10.077 ms |   5.79x |

这证明只读加载没有共享状态风险，但**不是产品模型**。产品模型是外部进程。

## 跨平台对照：Windows 与 CachyOS

同一台机器、同一份 Release 构建，分别在 Windows（MinGW/UCRT64 clang）和 CachyOS 上测量。 Windows 侧：`--pin p`、实时保护关闭、高性能电源计划、`--benchmark_repetitions=5`、`--benchmark_min_time=0.2s`，从 pwsh 直接启动（避免 shell 环境变量干扰）。

### Pass（ms，5k / 20k）

| Benchmark      |        CachyOS |        Windows |          倍数 |
| -------------- | -------------: | -------------: | ------------: |
| SemanticVerify |  0.724 / 3.158 |  1.048 / 4.863 | 1.45x / 1.54x |
| AbiVerify      |  1.260 / 5.581 |  2.157 / 9.612 | 1.71x / 1.72x |
| Lowering       | 1.995 / 10.080 | 2.617 / 13.903 | 1.31x / 1.38x |
| LoweringVerify | 2.869 / 13.296 | 3.869 / 20.338 | 1.35x / 1.53x |
| FullPipeline   | 4.698 / 21.150 | 6.978 / 31.394 | 1.49x / 1.48x |
| Compatibility  | 3.576 / 15.518 | 3.961 / 17.379 | 1.11x / 1.12x |

### Artifact（ms，5k / 10k）

| Benchmark         |          CachyOS |           Windows |          倍数 |
| ----------------- | ---------------: | ----------------: | ------------: |
| WriteAbix         |  20.229 / 42.486 |   25.778 / 57.089 | 1.27x / 1.34x |
| ReadAbix          |  17.021 / 38.404 |   21.592 / 49.257 | 1.27x / 1.28x |
| LoadAbiOnly       |  16.507 / 37.401 |   21.780 / 49.282 | 1.32x / 1.32x |
| LoadWithSemantic  | 61.275 / 133.081 |  76.694 / 171.786 | 1.25x / 1.29x |
| LoadWithLowering  | 55.130 / 108.595 |  76.768 / 170.008 | 1.39x / 1.57x |
| LoadArtifact      | 91.358 / 197.657 | 143.004 / 295.432 | 1.57x / 1.49x |
| BuildMetadata     |    1.566 / 3.198 |     2.424 / 4.890 | 1.55x / 1.53x |
| VerifyMetadata    |    1.036 / 2.063 |     1.045 / 2.087 | 1.01x / 1.01x |
| ModuleFromRegion  |    0.764 / 1.561 |     0.801 / 1.780 | 1.05x / 1.14x |
| Materialize       |    0.089 / 0.177 |     0.092 / 0.188 | 1.03x / 1.06x |
| MaterializeRegion |    0.853 / 1.725 |     0.907 / 1.993 | 1.06x / 1.16x |

### Cache（ms，200 / 1000 条目）

CachyOS 列是单文件 entry 重构和 POSIX `stat` 快速路径之后的 2026-11-07 复测。
Windows 列包含同一套单文件 entry、直接打开、按需校验和 64 KiB 以上 mmap；
POSIX `stat` 快速路径不适用于 Windows。

| Benchmark | CachyOS | Windows（优化后） | 倍数 |
| --- | ---: | ---: | ---: |
| StoreCold | 1.668 / 8.444 | 49.340 / 250.868 | 29.6x / 29.7x |
| LookupHit | 1.044 / 5.407 | 9.405 / 48.017 | 9.0x / 8.9x |
| LookupHitShallow | 0.723 / 3.796 | 4.435 / 22.798 | 6.1x / 6.0x |
| LookupMiss | 0.290 / 1.437 | 1.676 / 8.833 | 5.8x / 6.1x |
| LookupConcurrent | 0.371 / 1.795 | 3.597 / 17.267 | 9.7x / 9.6x |
| VerifyAll | 0.845 / 4.381 | 9.979 / 51.538 | 11.8x / 11.8x |
| DependencyChanged | 5.1 µs | 48 µs | 9.3x |
| LookupMissDiagnose / 200 | 52.86 ms | 506 ms | 9.6x |
| LookupHitLarge / 8 | 6.515 ms | 8.565 ms | 1.31x |

`LookupHitWithDeps` 不放进表格：Windows 正常条件下 1000 条目约 52 ms，但同一
二进制曾出现 645 ms 的离群 run。依赖读取成本受宿主文件系统状态影响很大。

重构后 CachyOS/Windows 倍数变大，是因为 Linux 侧变快；Windows 的残余成本是
NTFS 元数据操作，不是算法差异。

### Query 与多文件

- 精确查询在 Windows 上反而更快：`TypeExact` 0.95x / 0.85x、`TypeSubstring` 0.77x / 0.77x、`FunctionExact` 0.90x / 0.93x（5k / 20k）。
- 渲染与 context 慢 1.1–1.3x：`TypesJson` 1.32x / 1.33x，`ContextJson` 1.26x / 1.26x。
- 多文件串行 1.17–1.22x；线程池在 8 / 16 文件时 Windows 更快（并行加速比 6.40x vs 4.44x、6.88x vs 5.79x）。

### 读法

- **没有平台相关的复杂度回归**：所有 4x / 2x / 5x 增长斜率与 CachyOS 基本重合，上文门槛在 Windows 上同样满足。
- 常数因子分三档：`VerifyMetadata`、`Materialize` 和精确查询约 1.0x；大多数 pass、顺序 artifact I/O 和 JSON 渲染为 1.2–1.6x；cache 为 5–30x（`StoreCold` 最宽，Linux 已降到 8.4 µs/entry）。
- 精确查询在 Windows 更快，说明差距不在代码生成质量，而在**分配器 + 文件系统**； cache 的残余成本是 NTFS 元数据操作。
- Windows 上 cache 优化前后的单 entry 成本：`StoreCold` 533 → 251 µs，`LookupHit` 96 → 48 µs，浅命中 23 µs。

### 测量注意事项

- 表中的 `sd` 只代表同一次 run 内的重复噪声；跨 run 的绝对值可以漂移 0.5–1.4x，同 run 内横比优先。
- `AbiVerify` 在 Windows 上出现过一次 17.8 ms（同 run 内稳定），复跑 3 次均为 9.7–10.6 ms，已排除。
- 依赖文件读取受宿主文件系统状态影响：同一二进制、同一天，`LookupHitWithDeps/1000` 出现过 52 ms → 645 ms → 52 ms 的跳变。
- Windows 数字来自单次 run，作为参考而非替代 CachyOS 主基线。

## 优化历史

| 变更                              |              之前 |     之后 |
| --------------------------------- | ----------------: | -------: |
| Lowering / 20k                    |          552.7 ms |  10.1 ms |
| FullPipeline / 20k                |          525.7 ms |  21.2 ms |
| Compatibility / 20k               |          455.4 ms |  15.5 ms |
| Cache LookupMiss / 1000           |           1.454 s | 1.415 ms |
| Query FunctionExact / 5k          |         370.96 µs |  12.6 µs |
| LoadAbiOnly / 5k                  |   91.4 ms（full） |  16.5 ms |
| ContextJsonStream / 20k           | 46.7 ms（string） |  44.7 ms |
| Cache StoreCold / 1000（Linux）   |         15.306 ms |  8.444 ms |
| Cache LookupHit / 1000（Linux）   |          6.029 ms |  5.407 ms |
| Cache VerifyAll / 1000（Linux）   |          5.459 ms |  4.381 ms |
| Cache LookupHitShallow / 1000（Linux） |             — |  3.796 ms |
| Cache LookupMiss / 1000（Linux）  |          1.415 ms |  1.437 ms |
| Cache StoreCold / 1000（Windows） |          533.1 ms | 250.9 ms |
| Cache LookupHit / 1000（Windows） |           96.1 ms |  48.0 ms |

主要收益来自移除二次扫描、按需加载 sidecar，以及把 per-key cache 目录改为单个
entry 文件。并行化被刻意放在最后。

Linux 的 `LookupMiss/1000` 在直接打开 entry 后曾从 1.415 ms 变为稳定的 1.627 ms 均值
（+15%）。POSIX 专用 `stat` 快速路径把它补回到 1.437 ms（+1.5%）。`LookupHit/1000`
为 5.407 ms，比直接打开慢约 4%，但仍比重构前的 6.029 ms 快 10%。
`LookupMissDiagnose` 仍是独立的扫描诊断路径。

## 回归门槛

性能改动只有满足以下条件才接受：

- `ctest` 通过（当前 27/27）且 `boundary --strict` 通过；
- Pass 4x 输入增长 ≤ 6x；
- FullPipeline 4x 增长 ≤ 8x；
- Compatibility 4x 增长 ≤ 8x；
- Cache miss 5x 增长 ≤ 8x；POSIX `stat` 快速路径把 1000 条目 miss 控制在 +1.5%（1.437 ms vs 1.415 ms），hit 仍比重构前快 10%；
- Artifact load 和 query 渲染保持接近线性；
- `tools/run_bench.py` 无新增失败；
- 外部多进程吞吐不退化；
- 共享 cache 并发构建输出 digest 一致；
- Windows 上同样满足上述斜率门槛（见「跨平台对照」）。

## 注意事项

- 这些是本地 P-core 测量，不保证跨机器一致。
- governor、SMT、后台负载和内存带宽会影响绝对值。
- Windows 与 Linux 的绝对值受分配器和文件系统影响；cache 的 5–30x 差距是 NTFS 元数据成本，不是算法差异，详见「跨平台对照」。
- `LookupMissDiagnose` 故意昂贵；除非需要精确原因，否则使用默认 miss 路径。
- 多线程 benchmark 必须看 `real_time`；`cpu_time` 会低估。
- fixture 是合成负载；真实项目的 include 树、模板实例化和 I/O 可能不同。

## 相关文档

- [模块边界](../architecture/module-boundaries_zh.md)
- [AMC CLI](../amc/amc_zh.md)
- [C++ provider](../lang/cpp_zh.md)
- 优化历史记录在仓库内部工作笔记中。
