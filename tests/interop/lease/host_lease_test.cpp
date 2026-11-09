// C++ contract test for the AMC Host implementation of the ABIX lease C ABI.
//
// This is deliberately a standalone executable so it can be compiled by the
// interop test driver against the same ABIX checkout as the Rust/Zig consumers.
#include "host_lease.hpp"

#include "SKL/ABIX/Interface/Lease.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *what) {
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "  [FAIL] %s\n", what);
    }
}

using skl::abix::poly::invalid_module_instance;
using skl::abix::poly::module_state;
using skl::abix::poly::ModuleControl;
using skl::abix::poly::ModuleState;
using skl::amc::test::lease::HostLeaseRegistry;

abix_lease_registry *new_registry(const abix_lease_api &api) {
    abix_lease_registry *registry = nullptr;
    check(api.registry_create(api.host_context, &registry) == ABIX_SUCCESS, "registry_create");
    check(registry != nullptr, "registry handle is non-null");
    return registry;
}

void add_module(HostLeaseRegistry &host, ModuleControl &control, const char *name) {
    control.name = name;
    check(host.register_module(control) == ABIX_SUCCESS, "register_module");
    check(control.id != invalid_module_instance, "module instance assigned");
}

void registry_lifecycle(HostLeaseRegistry &host, const abix_lease_api &api) {
    ModuleControl control;
    add_module(host, control, "lease-lifecycle");
    abix_lease_registry *registry = new_registry(api);

    abix_lease lease = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, control.id, &lease) == ABIX_SUCCESS, "acquire");
    check(lease != ABIX_LEASE_INVALID, "lease token is valid");
    check(control.leases.load() == 1, "module lease count is 1");

    // A live lease blocks registry destruction and closes the registry.
    check(api.registry_destroy(api.host_context, registry) == ABIX_ERROR_BUSY, "destroy with live lease is busy");
    abix_lease rejected = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, control.id, &rejected) == ABIX_ERROR_UNLOADING,
        "acquire after CLOSING is rejected");
    check(rejected == ABIX_LEASE_INVALID, "rejected acquire leaves no token");

    api.release(api.host_context, registry, lease);
    check(control.leases.load() == 0, "release drops the module lease count");
    check(api.registry_destroy(api.host_context, registry) == ABIX_SUCCESS, "destroy after release");
    check(api.registry_destroy(api.host_context, registry) == ABIX_ERROR_INVALID, "double destroy is invalid");
}

void duplicate_release_and_token_reuse(HostLeaseRegistry &host, const abix_lease_api &api) {
    ModuleControl control;
    add_module(host, control, "lease-token");
    abix_lease_registry *registry = new_registry(api);

    abix_lease first = ABIX_LEASE_INVALID;
    abix_lease second = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, control.id, &first) == ABIX_SUCCESS, "first acquire");
    check(api.acquire(api.host_context, registry, control.id, &second) == ABIX_SUCCESS, "second acquire");
    check(first != second, "tokens are not reused while live");
    check(control.leases.load() == 2, "two live leases");

    api.release(api.host_context, registry, first);
    check(control.leases.load() == 1, "first release");
    api.release(api.host_context, registry, first);
    check(control.leases.load() == 1, "duplicate release is a no-op");

    api.release(api.host_context, registry, second);
    check(control.leases.load() == 0, "second release");
    check(api.registry_destroy(api.host_context, registry) == ABIX_SUCCESS, "destroy registry");
}

void draining_and_busy(HostLeaseRegistry &host, const abix_lease_api &api) {
    ModuleControl control;
    add_module(host, control, "lease-draining");
    abix_lease_registry *registry = new_registry(api);

    abix_lease lease = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, control.id, &lease) == ABIX_SUCCESS, "acquire before draining");

    // unregister begins draining, then reports busy because a lease is live.
    check(host.unregister_module(control.id) == ABIX_ERROR_BUSY, "unregister with live lease is busy");
    check(module_state(control) == ModuleState::draining, "module is draining after busy");

    abix_lease rejected = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, control.id, &rejected) == ABIX_ERROR_UNLOADING,
        "acquire while draining is rejected");
    check(rejected == ABIX_LEASE_INVALID, "draining reject leaves no token");

    // Destroying the object and releasing the lease is still allowed.
    api.release(api.host_context, registry, lease);
    check(control.leases.load() == 0, "draining lease released");
    check(host.unregister_module(control.id) == ABIX_SUCCESS, "unregister retry succeeds");
    check(module_state(control) == ModuleState::unloaded, "module is unloaded");
    check(api.registry_destroy(api.host_context, registry) == ABIX_SUCCESS, "destroy registry");
}

void unknown_module(const abix_lease_api &api) {
    abix_lease_registry *registry = new_registry(api);
    abix_lease lease = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, 0xdeadbeefu, &lease) == ABIX_ERROR_NOT_FOUND,
        "unknown module is not found");
    check(lease == ABIX_LEASE_INVALID, "unknown module leaves no token");
    check(api.registry_destroy(api.host_context, registry) == ABIX_SUCCESS, "destroy registry");
}

void independent_registries(HostLeaseRegistry &host, const abix_lease_api &api) {
    ModuleControl control;
    add_module(host, control, "lease-independent");
    abix_lease_registry *registry_a = new_registry(api);
    abix_lease_registry *registry_b = new_registry(api);

    abix_lease token_a = ABIX_LEASE_INVALID;
    abix_lease token_b = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry_a, control.id, &token_a) == ABIX_SUCCESS, "registry A acquire");
    check(api.acquire(api.host_context, registry_b, control.id, &token_b) == ABIX_SUCCESS, "registry B acquire");
    check(token_a == token_b, "tokens are registry-scoped and may have the same value");
    check(control.leases.load() == 2, "both registries hold a lease");

    api.release(api.host_context, registry_a, token_a);
    check(control.leases.load() == 1, "releasing A does not affect B");
    api.release(api.host_context, registry_b, token_b);
    check(control.leases.load() == 0, "releasing B");

    check(api.registry_destroy(api.host_context, registry_a) == ABIX_SUCCESS, "destroy A");
    check(api.registry_destroy(api.host_context, registry_b) == ABIX_SUCCESS, "destroy B");
}

void concurrent_duplicate_release(HostLeaseRegistry &host, const abix_lease_api &api) {
    ModuleControl control;
    add_module(host, control, "lease-race");
    abix_lease_registry *registry = new_registry(api);

    abix_lease lease = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, control.id, &lease) == ABIX_SUCCESS, "acquire for race");
    check(control.leases.load() == 1, "one lease before race");

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] { api.release(api.host_context, registry, lease); });
    for (std::thread &thread : threads)
        thread.join();

    check(control.leases.load() == 0, "concurrent duplicate release decrements at most once");
    api.release(api.host_context, registry, lease);
    check(control.leases.load() == 0, "post-race duplicate release is a no-op");
    check(api.registry_destroy(api.host_context, registry) == ABIX_SUCCESS, "destroy registry after race");
}

void concurrent_acquire_unregister(HostLeaseRegistry &host, const abix_lease_api &api) {
    ModuleControl control;
    add_module(host, control, "lease-acquire-race");
    abix_lease_registry *registry = new_registry(api);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> successful{0};
    std::thread worker([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            abix_lease lease = ABIX_LEASE_INVALID;
            if (api.acquire(api.host_context, registry, control.id, &lease) == ABIX_SUCCESS) {
                successful.fetch_add(1, std::memory_order_relaxed);
                api.release(api.host_context, registry, lease);
            } else {
                std::this_thread::yield();
            }
        }
    });

    // Let the worker acquire at least once so unregister actually races with a
    // live lease instead of winning before the worker starts.
    for (int spin = 0; spin < 100'000 && successful.load(std::memory_order_relaxed) == 0; ++spin)
        std::this_thread::yield();
    check(successful.load(std::memory_order_relaxed) > 0, "worker acquired before unregister");

    // Retry unregister until it succeeds. BUSY means the worker still holds a
    // lease; the module stays draining and rejects new acquires in the meantime.
    abix_status status = ABIX_ERROR_BUSY;
    for (int attempt = 0; attempt < 100'000 && status == ABIX_ERROR_BUSY; ++attempt)
        status = host.unregister_module(control.id);
    check(status == ABIX_SUCCESS, "unregister eventually succeeds under acquire race");
    stop.store(true, std::memory_order_relaxed);
    worker.join();

    // Once unregistered, no new acquire can succeed.
    abix_lease rejected = ABIX_LEASE_INVALID;
    check(api.acquire(api.host_context, registry, control.id, &rejected) == ABIX_ERROR_NOT_FOUND,
        "acquire after unregister is rejected");
    check(rejected == ABIX_LEASE_INVALID, "post-unregister acquire leaves no token");
    check(api.registry_destroy(api.host_context, registry) == ABIX_SUCCESS, "destroy registry after race");
}

}   // namespace

int main() {
    HostLeaseRegistry host;
    const abix_lease_api api = host.make_api();

    check(api.abi_version == ABIX_LEASE_API_VERSION, "api version");
    check(api.struct_size == sizeof(abix_lease_api), "api struct size");
    check(api.host_context == &host, "api host context");
    check(api.registry_create != nullptr
              && api.registry_destroy != nullptr
              && api.acquire != nullptr
              && api.release != nullptr,
        "all function pointers are present");

    registry_lifecycle(host, api);
    duplicate_release_and_token_reuse(host, api);
    draining_and_busy(host, api);
    unknown_module(api);
    independent_registries(host, api);
    concurrent_duplicate_release(host, api);
    concurrent_acquire_unregister(host, api);

    if (failures == 0) {
        std::printf("lease host: OK\n");
        return 0;
    }
    std::fprintf(stderr, "lease host: %d failure(s)\n", failures);
    return 1;
}
