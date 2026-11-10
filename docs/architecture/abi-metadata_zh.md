# ABI 元数据与共享声明

<p align="center">
  中文 · <a href="abi-metadata.md">English</a>
</p>

## 状态

| 项                                            | 状态          |
| --------------------------------------------- | ------------- |
| Owner handle 布局（`ABIX_OWNER_HANDLE_JSON`） | v0.4.0 已提供 |
| Lease C ABI（`LeaseABI.h`）                   | v0.4.0 已提供 |
| Lease API 元数据（`ABIX_LEASE_API_JSON`）     | v0.4.x 计划   |
| 生成的 lease 声明（Rust/C++）                 | v0.4.x 计划   |

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
        └── ABIX_LEASE_API_JSON       （v0.4.x 计划）
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
- Rust 投影里第二份 `abix_lease_api` / `abix_status` 声明。
- 各语言各自维护的 `struct_size`、字段 offset 和 status 值。
- C 头文件与生成绑定之间的静默漂移。

## v0.4.x 交付项

1. ABIX 增加 `cmake/lease_api.json`，安装为 `ABIX_LEASE_API_JSON`。
2. 字段：`abi_version`、`struct_size`、`host_context`、`registry_create`、`registry_destroy`、`acquire`、`release`，以及稳定的 `abix_status` 值。
3. AMC CMake 读取 JSON，为生成 lease 绑定的目标定义布局/版本事实。
4. Rust 投影从这些事实生成 lease 声明；C++ 使用 `LeaseABI.h` 并生成 static_assert。
5. 生成的 `LeaseGuard` 使用共享声明。

该 JSON 是附加元数据，不改变 lease ABI、`abi_version` 或 `struct_size` 规则。旧消费者仍可继续使用 `LeaseABI.h`。

## 为什么这对 `LeaseGuard` 重要

`LeaseGuard<OwnerOf*>` 需要 `abix_lease_api`、`abix_lease_registry`、`abix_lease` 和 `abix_status`。如果每种生成语言各自声明一份，guard 可能编译到过期布局。共享元数据让 C 头文件、AMC 运行时和每个生成的 guard 使用同一份声明。
