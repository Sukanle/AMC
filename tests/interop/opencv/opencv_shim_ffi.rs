// C++ shim around OpenCV's C++ API. Kept in a separate module so AMC's Rust
// frontend sees only the bridge surface, not these imported C symbols.

#[link(name = "opencv_shim")]
unsafe extern "C" {
    pub fn opencv_shim_mat_new(rows: i32, cols: i32) -> *mut core::ffi::c_void;
    pub fn opencv_shim_mat_destroy(mat: *mut core::ffi::c_void);
    pub fn opencv_shim_mat_set(mat: *mut core::ffi::c_void, row: i32, col: i32, value: f64) -> i32;
    pub fn opencv_shim_mat_get(mat: *const core::ffi::c_void, row: i32, col: i32) -> f64;
    pub fn opencv_shim_mat_sum(mat: *const core::ffi::c_void) -> f64;
}
