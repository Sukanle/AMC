# Lease 运行时与生成的 guard

<p align="center">
  中文 · <a href="lease-runtime.md">English</a>
</p>

## 状态

| 层                | 位置                                    | 状态        |
| ----------------- | --------------------------------------- | ----------- |
| ABIX lease C ABI  | `SKL/ABIX/Interface/LeaseABI.h`         | ABIX v0.4.0 |
| Host 运行时       | `SKL/AMC/Support/LeaseRegistry.h`       | AMC v0.4.0  |
| 生成的 C++ guard  | C++ 投影中的 `LeaseGuard<OwnerOf*>`     | AMC v0.4.0  |
| 生成的 Rust guard | `amc_generated.rs` 中的 `LeaseGuard<O>` | AMC v0.4.0  |

## 两层，一个生命周期

lease 路径刻意分成两层：

```text
Host 运行时（AMC）                          Consumer 包装
┌─────────────────────────────┐            ┌──────────────────────────────┐
│ skl::amc::support::         │            │ LeaseGuard<OwnerOf*>         │
│ LeaseRegistry               │            │                              │
│                             │            │ acquire(api, registry, m)    │
│ registry_create/destroy     │abix_lease  │ create() -> owner            │
│ acquire/release             │_api        │ Drop: destroy -> release     │
│ module table / draining     │            │                              │
└─────────────────────────────┘            └──────────────────────────────┘
```

- `LeaseRegistry` 是 Host：拥有 registry、module table、lease 计数和 `abix_lease_api` 函数表。
- `LeaseGuard` 是 consumer 侧策略：持有 owner handle、registry handle 和 lease token，并强制 `acquire -> create -> destroy -> release`。

guard 不实现 registry，runtime 也不知道任何生成的 owner 类型。两者共享的唯一契约是 ABIX C ABI。

## Host 运行时

```cpp
#include "SKL/AMC/Support/LeaseRegistry.h"

skl::amc::support::LeaseRegistry host;
abix_lease_api api = host.make_api();

abix_lease_registry *registry = nullptr;
if (host.create_registry(&registry) != ABIX_SUCCESS) return 1;

skl::abix::poly::ModuleControl control;
control.name = "demo";
if (host.register_module(control) != ABIX_SUCCESS) return 2;

// 告诉 provider 哪个 module instance 拥有它创建的 handle。
amc_module_set_instance(control.id);
```

`unregister_module` 会开始 draining；只要还有 live lease，就返回 `ABIX_ERROR_BUSY`。在所有 guard 释放 lease 之前，模块不会卸载。

## 生成的 C++ guard

```cpp
using Guard = skl::amc::generated::LeaseGuard<skl::amc::generated::OwnerOfWidget>;

auto guard = Guard::acquire(&api, registry, control.id, [] {
    return skl::amc::generated::OwnerOfWidget(widget_make(7));
});
if (!guard) return 3;
```

guard 是 move-only。析构时先调用 owner 的 destroy，再调用 `api.release(host_context, registry, lease)`。

`LeaseGuard::adopt` 接收 Host 已经 acquire 的 lease 和 owner handle。

## 生成的 Rust guard

```rust
let guard = LeaseGuard::<OwnerOfWidget>::acquire(api, registry, module, || {
    OwnerOfWidget::from_raw(widget_make(7))
})
.expect("lease guard");
```

生成的 guard 会：

- 拒绝 null API、不支持的 `abi_version`、过短的 `struct_size` 以及缺失的 `acquire`/`release`；
- 在 `create` 返回 `None` 时释放 lease；
- 在 `Drop` 中先 destroy owner，再 release lease；
- 保持 move-only，不实现 `Clone`。

## 失败与生命周期规则

- `acquire` 必须先于 `create`；不存在 create/acquire 窗口。
- create 失败必须立即 release lease。
- 先 `destroy`，后 `release`。
- registry 仍有 live lease 时，`registry_destroy` 返回 `ABIX_ERROR_BUSY`。
- 模块仍有 live lease 时，`unregister_module` 返回 `ABIX_ERROR_BUSY`。
- 同一个 token 的重复 release 是 no-op。
- `api`、`registry` 和 `host_context` 必须比所有 guard 活得更久。

## 声明与元数据

C++ guard 直接使用 ABIX 的 `LeaseABI.h`。Rust 投影目前会生成一份 `#[repr(C)] abix_lease_api` 声明，并带 size/offset 编译期断言；[ABI 元数据与共享声明](abi-metadata_zh.md) 描述了计划中的 `ABIX_LEASE_API_JSON` 路径，用来消除这份重复声明。

## 测试

- `tests/interop/lease/lease_test.py`：Host registry 生命周期、draining、重复 release、并发，以及 C++/Rust/Zig consumer。
- `tests/providers/rust/box_adapt_test.py`：对真实 Rust provider 使用生成的 C++ `LeaseGuard` 和 Rust `LeaseGuard`（acquire、create 失败、adopt、 module instance 校验、先 destroy 后 release、无效 API 拒绝）。

## 边界

- `Support/LeaseRegistry` 依赖 ABIX `Lease.h` / `LeaseABI.h`；不依赖 frontend、backend 或 CLI 命令。
- 生成的 guard 只依赖 ABIX C ABI，不依赖 AMC 的 runtime 类型，因此 consumer 可以换用其他 Host 实现。
