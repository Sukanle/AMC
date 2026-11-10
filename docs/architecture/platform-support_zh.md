# 平台支持

<p align="center">
  中文 · <a href="platform-support.md">English</a>
</p>

## 支持的平台

AMC 与 ABIX 只支持 **64 位平台**。

| 指针宽度 | 状态               |
| -------- | ------------------ |
| 64 位    | 支持               |
| 32 位    | 不支持；无限期延后 |
| 16 位    | 不支持             |

32 位支持不在当前路线图中，不计划进入 v0.4.x 或 v0.5.0；AMC 也不会把 32 位目标当作“尽力而为”的配置。Rust 前端会在投影任何 ABI 之前，以显式诊断拒绝 `[target] pointer_bits != 64`。

## 为什么 32 位不在范围内

### 64 位 / 128 位身份 hash 是结构性依赖

AMC/ABIX 使用 64 位和 128 位 hash 表示类型身份、布局身份、函数签名、artifact 身份和元数据。这些不是可选的缓存键，而是 `.abix`、`.semantic`、`.lowering` 和生成的 metadata region 所承载的稳定身份模型。

在 32 位平台上：

- 原生 64 位算术需要模拟或辅助例程；
- 把身份截断到 32 位会产生碰撞并破坏稳定身份；
- 为 32 位重新设计身份与 hash 模型会改变 wire format 和所有消费者。

因此 32 位支持不是加一个可移植性开关，而是重新设计核心身份模型。

### ABIX 元数据和查询表对指针/地址空间敏感

ABIX 记录 type、field、function、symbol、interface、error 和 canonical-layout 表；查询路径还维护 module 与 metadata 状态。32 位进程的地址空间小得多，表密集型路径的压力显著更高：

- 指针密集型记录占用地址空间的比例更大；
- 大型模块和多模块查询容易造成 32 位地址空间碎片化或耗尽；
- metadata region 和查询工作集的余量更小，性能和内存压力明显劣于 64 位；
- 64 位 module instance、lease token 和 owner handle 在许多 32 位目标上需要拆成 64 位加载/存储或模拟。

问题不只是速度；地址空间预算会改变 metadata 与查询表的设计。

### owner 与 lease ABI 是 64 位

`abix_owner_handle.module_instance`、`abix_lease`、`abix_module_instance` 都是 `uint64_t`。owner/lease 生命周期假定 64 位身份空间和具有稳定 64 位整数表示的目标 ABI。支持 32 位意味着在现有模型之上再引入按目标变化的 owner/owned-view 布局和 lease 处理。

### 测试与维护成本

支持 32 位至少需要：

- i386 SysV、ARMv7 AAPCS、Windows x86 等 ABI 变体；
- 目标相关的 owner/owned-view 布局与 ABI 检查；
- 跨目标 backend 测试和 CI runner；
- 针对没有当前用户需求的新配置持续调试。

当前需求不足以支撑这部分维护成本。

## “不支持”的含义

- 没有 32 位 CI 或测试矩阵。
- 不保证生成的投影、owner/owned-view 布局、metadata 或查询结果在 32 位上正确。
- 32 位目标应当 fail closed（`pointer_bits != 64`），而不是输出尽力而为的结果。
- 仅与 32 位执行相关的 bug report 不在处理范围内。

## 何时重新考虑

如果要重新考虑 32 位，需要一份覆盖以下内容的设计提案：

1. 64 位/128 位身份与 hash 策略；
2. metadata/query 表布局与地址空间预算；
3. 各目标 ABI 下的 owner/owned-view/lease 布局；
4. target-aware backend 布局与代码生成；
5. 真实的跨目标测试矩阵。

在此之前，32 位将无限期延后，不属于主线路线图。

## 相关文档

- [ABIX lease ABI](https://github.com/Sukanle/ABIX/blob/main/docs/abix/lease.md)
- [ABIX STD 布局](https://github.com/Sukanle/ABIX/blob/main/docs/abix/std.md)
