# ABI 元数据与共享声明

<p align="center">
  中文 · <a href="abi-metadata.md">English</a>
</p>

## 状态

| 项                                            | 状态               |
| --------------------------------------------- | ------------------ |
| Owner handle 布局（`ABIX_OWNER_HANDLE_JSON`） | v0.4.0 已提供      |
| Lease C ABI（`LeaseABI.h`）                   | v0.4.0 已提供      |
| Lease API 元数据（`ABIX_LEASE_API_JSON`）     | ABIX v0.4.0 已提供 |
| 生成的 lease 声明（Rust/C++）                 | AMC v0.4.0 已提供  |

## 重复声明的问题

跨语言 ABI 只有一个规范定义，但有很多消费者。没有共享元数据时，每个消费者都会维护自己的一份拷贝：

- C++ 直接 include ABIX 头文件；
- Rust 声明一份 `#[repr(C)]` 镜像；
- Zig 声明一份 `extern struct`；
- AMC 里硬编码 offset 或字段大小。

每一份拷贝都可能漂移。字段插入、`struct_size` 变化或新增 status 值，都会从编译期错误变成运行期 bug。

## 单一事实来源

ABIX 拥有 C ABI 头文件，并同时发布机器可读的布局元数据。AMC 读取该元数据，再生成各语言声明。

```text
ABIX OwnerABI.h / LeaseABI.h          （权威 C ABI）
        │
        ├── ABIX_OWNER_HANDLE_JSON    （v0.4.0）
        └── ABIX_LEASE_API_JSON       （v0.4.0）
                    │
                    ▼
             AMC 配置期/代码生成
                    │
        ┌───────────┴───────────┐
        ▼                       ▼
  C++ 声明                Rust/Zig 声明
  （alias + static_assert）（生成的 repr(C)/extern struct）
```

生成代码包含编译期检查，布局漂移会直接使构建失败：

```cpp
static_assert(sizeof(abix_owner_handle) == 32);
static_assert(offsetof(abix_owner_handle, module_instance) == 24);
```

```rust
const _: () = assert!(core::mem::size_of::<abix_lease_api>() == ...);
const _: () = assert!(core::mem::offset_of!(abix_lease_api, acquire) == ...);
```

## 消除了哪些重复声明

- AMC 中手工维护的 owner-handle offset；`OwnerLayout.h` 现在在配置期读取 `ABIX_OWNER_HANDLE_JSON`。
- Rust 投影里手工维护的 `abix_lease_api` size/offset/status 值。
- 各语言各自维护的 `struct_size`、字段 offset 和 status 值。
- C 头文件与生成绑定之间的静默漂移。

## 已实现内容

1. ABIX v0.4.0 发布 `cmake/lease_api.json`，安装为 `ABIX_LEASE_API_JSON`。
2. 文件记录 `abi_version`、`struct_size`、`align`、字段 offset 和稳定的 `abix_status` 值。
3. AMC CMake 读取 JSON，为 `amc-rust` 定义 lease 布局/版本事实。
4. Rust 投影用这些事实生成常量和编译期 size/offset 断言；C++ 直接使用 `LeaseABI.h`。
5. 生成的 `LeaseGuard` 使用共享声明。

该 JSON 是附加元数据，不改变 lease ABI、`abi_version` 或 `struct_size` 规则。旧消费者仍可继续使用 `LeaseABI.h`。

## 仍待完成的可选工作

- 可选：由 ABIX 提供完整的 Rust/Zig 绑定。Rust 结构拼写目前仍由 AMC 生成，但布局和状态事实已经来自元数据。

## 为什么这对 `LeaseGuard` 重要

`LeaseGuard<OwnerOf*>` 需要 `abix_lease_api`、`abix_lease_registry`、`abix_lease` 和 `abix_status`。如果每种生成语言各自声明一份，guard 可能编译到过期布局。共享元数据让 C 头文件、AMC 运行时和每个生成的 guard 使用同一份声明。
