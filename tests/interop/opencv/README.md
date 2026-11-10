# OpenCV Rust bridge test

A small end-to-end bridge:

```text
#![deny(unsafe_code)] consumer
        -> AMC-generated extern "C" thunks and safe facade
        -> Rust provider (`opencv_bridge.rs`)
        -> C++ shim (`opencv_shim.cpp`)
        -> OpenCV `cv::Mat`
```

OpenCV 5 removed the legacy C API, so the Rust provider calls a tiny C++ shim that wraps `cv::Mat`. The provider keeps `OpencvMat` `#[repr(C)]` and exposes ordinary `&OpencvMat` / `&mut OpencvMat` functions; AMC turns `Box<OpencvMat>` into an ABIX owner handle and the consumer only uses `skl::abix::safe` plus `OwnerOfOpencvMat::as_ref` / `as_mut`.

Run standalone with an AMC bin directory:

```bash
tests/interop/opencv/opencv_bridge_test.py build/Release/bin .
```

The test needs `pkg-config` (`opencv5`, `opencv4` or `opencv`), a C++ compiler and `rustc`; it skips on Windows or when OpenCV is unavailable.
