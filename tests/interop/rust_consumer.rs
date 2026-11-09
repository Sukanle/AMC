// M1 interop consumer: a real Rust program that links the native demo library
// and calls it through the AMC-generated projection.
//
// The projection (`amc_generated.rs`) is produced by `amc generate -l rust`
// and declares the `#[repr(C)]` type plus the `extern "C"` entry points. Its
// compile-time assertions (size / align / field offsets) are the first line of
// defence: if the library and the metadata disagree, this file fails to build.

#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}

use skl::abix::*;

fn main() {
    // Value-only entry points are generated as safe wrappers: no unsafe block
    // is required here. Pointer and owner operations stay `unsafe` because the
    // caller still has to uphold their raw contract.
    assert_eq!(demo_add(2, 3), 5, "demo_add");

    unsafe {
        // Call taking a typed pointer to a value type defined on the Rust side.
        let v = DemoVec2 { x: 4, y: 2.5 };
        let scaled = demo_scale(&v, 2.0);
        assert_eq!(scaled, 13.0, "demo_scale");

        // Ownership wrapper: `create` takes ownership, `drop` releases through
        // the declared destroy entry point -- the consumer never frees foreign
        // memory itself.
        let owned = OwnedDemoVec2::create(7, 1.5).expect("create owned");
        assert_eq!(owned.as_ref().x, 7, "owned.x");
        assert_eq!(owned.as_ref().y, 1.5, "owned.y");

        // Lifetime/destruction contract, made observable by the library.
        assert_eq!(demo_live_count(), 1, "one live handle");
        drop(owned);
        assert_eq!(demo_live_count(), 0, "handle released on drop");

        // `into_raw` leaks the handle back out for manual handoff.
        let raw = OwnedDemoVec2::create(1, 2.0).expect("create").into_raw();
        assert_eq!(demo_live_count(), 1, "raw handle live");
        assert_eq!(demo_vec2_destroy(raw), 0, "manual destroy");
        assert_eq!(demo_live_count(), 0, "manual release");

        // Error boundary: the generated Result wrapper turns the C status code
        // into a typed result; the native exception was caught in the library.
        assert_eq!(demo_div_result(10, 2), Ok(5), "div ok");
        let err = demo_div_result(10, 0).expect_err("div by zero");
        assert_eq!(err.0, 1, "div error status");
        assert_eq!(err.name(), "invalid_input", "named error code");
        assert_eq!(err.condition_name(), "invalid_argument", "generic condition");

        // Unknown error codes must survive the Rust projection. The library
        // returns 99, which is outside the contract's known status values.
        let unknown = demo_div_unknown_result(10, 2).expect_err("unknown status");
        assert_eq!(unknown.0, 99, "unknown error code is preserved");

        // Bind the same handle-shaped interface contract from the shared
        // artifact and call it through the native library.
        let mut scaled_value = DemoVec2 { x: 4, y: 2.5 };
        unsafe extern "C" fn scale_adapter(object: *mut core::ffi::c_void, factor: f64) -> f64 {
            unsafe { demo_scale_handle(object as *const core::ffi::c_void, factor) }
        }
        let scale_method = InterfaceMethodEntry {
            signature_lo: IVectorScaleProxy::DEMO_SCALE_HANDLE_SIGNATURE.0,
            signature_hi: IVectorScaleProxy::DEMO_SCALE_HANDLE_SIGNATURE.1,
            entry: scale_adapter as usize,
            slot: IVectorScaleProxy::DEMO_SCALE_HANDLE_SLOT,
            flags: 0,
        };
        let scale_proxy = IVectorScaleProxy::bind(
            (IVectorScaleProxy::ID_LO, IVectorScaleProxy::ID_HI),
            IVectorScaleProxy::VERSION,
            &mut scaled_value as *mut DemoVec2 as *mut core::ffi::c_void,
            &[scale_method],
            None,
        )
        .expect("interface bind");
        assert_eq!(scale_proxy.demo_scale_handle(2.0), 13.0, "interface call");

        // Runtime metadata: parse the ABIX Metadata Region embedded in this
        // projection and check the facts it records against the Rust type.
        let region = skl::abix::metadata::Region::open(&skl::abix::AMC_METADATA_REGION).expect("metadata region");
        let demo = region.find_type_by_name("DemoVec2").expect("DemoVec2 in metadata");
        assert_eq!(demo.size, 16, "metadata size");
        assert_eq!(demo.align, 8, "metadata align");
        assert_eq!(demo.id.lo, DemoVec2::ABIX_TYPE_ID_LO, "metadata type id lo");
        assert_eq!(demo.id.hi, DemoVec2::ABIX_TYPE_ID_HI, "metadata type id hi");
        let x = region.find_field(demo.id, "x").expect("field x in metadata");
        let y = region.find_field(demo.id, "y").expect("field y in metadata");
        assert_eq!((x.offset, y.offset), (0, 8), "metadata field offsets");
        let add = region.find_function_by_name("demo_add").expect("demo_add in metadata");
        assert_eq!(add.calling_convention, 1, "demo_add is cdecl");

        // Ownership contract (read at run time, never inferred): the factory
        // returns an owned value released by the declared destroy entry point;
        // `demo_scale` only borrows its argument; destroy consumes its handle.
        let create_index = region.function_index_by_name("demo_vec2_create").expect("create index");
        let ret = region.find_ownership(1, create_index).expect("create return contract");
        assert_eq!(ret.mode, skl::abix::metadata::OWNERSHIP_OWNED, "create returns owned");
        assert_eq!(
            ret.release_function,
            region
                .function_index_by_name("demo_vec2_destroy")
                .expect("destroy index"),
            "release entry point"
        );
        let scale = region.find_function_by_name("demo_scale").expect("demo_scale");
        let borrowed = region
            .find_ownership(0, scale.parameter_begin)
            .expect("scale param contract");
        assert_eq!(borrowed.mode, skl::abix::metadata::OWNERSHIP_BORROWED, "scale borrows");
        let destroy = region.find_function_by_name("demo_vec2_destroy").expect("destroy");
        let consumed = region
            .find_ownership(0, destroy.parameter_begin)
            .expect("destroy param contract");
        assert_eq!(consumed.mode, skl::abix::metadata::OWNERSHIP_OWNED, "destroy consumes");

        // The error boundary contract is discoverable from the same directory.
        let div_index = region.function_index_by_name("demo_div").expect("div index");
        let ec = region.find_error(div_index).expect("div error contract");
        assert_eq!(ec.status_source, 0, "div status is the return value");
        assert_eq!(ec.success_value, 0, "div success value");
        assert_eq!(ec.value_parameter, 2, "div value parameter");

        // Container accessor adapters are discoverable from the region too, with
        // their borrow semantics (the invalidation rule is the caller's contract).
        assert!(region.view_adapter_count() >= 1, "view adapter in region");
        assert!(region.range_adapter_count() >= 1, "range adapter in region");
        assert_eq!(region.view_adapter_at(0).unwrap().semantics, 1, "view is borrowed");

        // Non-intrusive polymorphism: the interface contract, its methods and
        // the concrete implementation bindings are discoverable from the region
        // without reading any C++ private vtable.
        assert!(region.interface_count() >= 1, "interface in region");
        let iface = region.interface_at(0).expect("interface record");
        assert_eq!(iface.method_count, 2, "interface method count");
        assert!(
            region.interface_method_count() >= iface.method_count,
            "interface methods in region"
        );
        let first_method = region.interface_method_at(0).expect("interface method");
        assert!(
            first_method.signature.lo != 0 || first_method.signature.hi != 0,
            "method signature present"
        );
        assert!(first_method.parameter_count >= 1, "method parameters recorded");
        assert!(
            region.interface_implementation_count() >= 1,
            "interface implementation in region"
        );
        let implementation = region.interface_implementation_at(0).expect("implementation");
        assert_eq!(implementation.binding_count, 2, "implementation binds every method");
        assert!(region.method_binding_count() >= 2, "method bindings in region");
        let binding = region.method_binding_at(0).expect("method binding");
        assert!(
            binding.native_function.lo != 0 || binding.native_function.hi != 0,
            "native adapter recorded"
        );

        // Target platform verification, read from the same region.
        let target = region.target().expect("target description");
        assert_eq!(target.pointer_bits, 64, "pointer width");
        assert_eq!(target.endianness, 1, "little-endian");
        let expected = skl::abix::metadata::TargetExpectation {
            arch: 1,
            os: 1,
            target_abi: 1,
            pointer_bits: 64,
            endianness: 1,
            calling_convention: 1,
        };
        assert_eq!(
            region.verify_target(expected),
            skl::abix::metadata::TARGET_COMPATIBLE,
            "target compatible"
        );
        let wrong = skl::abix::metadata::TargetExpectation {
            arch: 2,
            ..Default::default()
        };
        assert_eq!(
            region.verify_target(wrong),
            skl::abix::metadata::TARGET_INCOMPATIBLE,
            "target incompatible"
        );

        // Structured ABI verification: platform, layout and calling convention
        // are reported independently, so a reader can tell what matched.
        let abi = region.verify_abi(expected);
        assert_eq!(abi.platform, skl::abix::metadata::TARGET_COMPATIBLE, "abi platform");
        assert_eq!(abi.layout, skl::abix::metadata::TARGET_COMPATIBLE, "abi layout");
        assert_eq!(
            abi.calling_convention,
            skl::abix::metadata::TARGET_COMPATIBLE,
            "abi calling convention"
        );

        // ABIX-STD borrowed span (M3): a non-owning view over library memory.
        // The consumer reads it and never releases it; `demo_sum` takes it by
        // value and returns the total.
        let span = demo_numbers();
        assert_eq!(span.len, 5, "span len");
        let mut manual: i64 = 0;
        for i in 0..span.len {
            manual += unsafe { *span.data.add(i as usize) } as i64;
        }
        assert_eq!(manual, 15, "span contents");
        assert_eq!(demo_sum(span), 15, "demo_sum");

        // A non-canonical view is lowered to the boundary entity by a generated
        // adapter: instantiation succeeded, but it is not the ABIX-STD type.
        let reversed = demo_numbers_rev();
        let adapted = abix_view_rev_i32_to_abix_span_i32(reversed);
        assert_eq!(adapted.len, 5, "adapted len");
        assert_eq!(demo_sum(adapted), 15, "adapted sum");

        // ABIX-STD string view (P0): borrowed, read-only UTF-8 text.
        let greeting = demo_greeting();
        assert_eq!(greeting.len, 5, "greeting len");
        assert_eq!(greeting.encoding, 0, "greeting is utf8");
        assert_eq!(demo_str_len(greeting), 5, "demo_str_len");

        // A vector-like container lowered to a sequence view through accessors:
        // zero-copy borrow, internal layout never exposed.
        let buffer = demo_buffer_create();
        assert!(!buffer.is_null(), "buffer created");
        assert_eq!(demo_buffer_push(buffer, 7), 0);
        assert_eq!(demo_buffer_push(buffer, 8), 0);
        let buffer_view = DemoBuffer_view(buffer);
        assert_eq!(buffer_view.len, 2, "buffer view len");
        assert_eq!(demo_sum(buffer_view), 15, "buffer sum");
        assert_eq!(demo_buffer_destroy(buffer), 0, "buffer destroyed");

        // ABIX-STD P1 contracts: optional, result and callback.
        let present = demo_optional(1);
        assert_eq!(present.discriminant, 1, "optional present");
        assert_eq!(present.value, 42, "optional value");
        assert_eq!(demo_optional(0).discriminant, 0, "optional absent");

        let ok = demo_result(5);
        assert_eq!(ok.status, 0, "result ok status");
        assert_eq!(ok.value, 10, "result value");
        assert_eq!(demo_result(-1).status, 1, "result error status");

        let callback = demo_callback();
        assert!(!callback.invoke.is_null(), "callback invoke");
        assert!(!callback.context.is_null(), "callback context");

        // ABIX-STD P1 range: length + indexed access, no contiguous assumption.
        let set = demo_set_create();
        assert!(!set.is_null(), "set created");
        assert_eq!(demo_set_insert(set, 3), 0);
        assert_eq!(demo_set_insert(set, 4), 0);
        assert_eq!(DemoSet_len(set), 2, "range len");
        assert_eq!(DemoSet_at(set, 0), 3, "range at 0");
        assert_eq!(DemoSet_at(set, 1), 4, "range at 1");
        assert_eq!(demo_set_destroy(set), 0, "set destroyed");

        // P2: a Map via an opaque handle + accessors (no unified container).
        let map = demo_map_create();
        assert!(!map.is_null(), "map created");
        assert_eq!(demo_map_put(map, 1, 10), 0);
        assert_eq!(demo_map_put(map, 2, 20), 0);
        assert_eq!(demo_map_size(map), 2, "map size");
        let mut out: core::ffi::c_int = 0;
        assert_eq!(demo_map_get(map, 1, &mut out), 0, "map get ok");
        assert_eq!(out, 10, "map get value");
        assert_eq!(demo_map_get(map, 99, &mut out), 2, "map get missing");
        assert_eq!(demo_map_destroy(map), 0, "map destroyed");

        // The lowering result is discoverable from the metadata region, read-only:
        // the reader learns an adapter is required but never runs it implicitly.
        let rev_index = region.type_index_by_name("abix_view_rev_i32").expect("rev type index");
        let lowering = region.find_lowering(rev_index).expect("lowering record");
        assert_eq!(
            lowering.status,
            skl::abix::metadata::LOWERING_NEEDS_ADAPTER,
            "rev needs adapter"
        );
        assert_eq!(
            lowering.reason,
            skl::abix::metadata::LOWERING_REASON_LAYOUT,
            "rev lowering reason"
        );
        assert_eq!(
            lowering.contract_index,
            region.type_index_by_name("abix_span_i32").expect("span type index"),
            "lowering contract type"
        );

        println!(
            "interop ok: add=5 scale={scaled} div=5 err=1 types={}",
            region.header.type_count
        );
    }
}
