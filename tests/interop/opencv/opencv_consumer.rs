// Safe consumer of the AMC-generated OpenCV bridge.

#![deny(unsafe_code)]

#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}

use skl::abix::safe::*;

fn main() {
    let mut matrix = opencv_mat_new(2, 3).expect("OpenCV matrix");
    assert_eq!(opencv_mat_set(matrix.as_mut().expect("matrix"), 0, 0, 3.5), 0);
    assert_eq!(opencv_mat_set(matrix.as_mut().expect("matrix"), 1, 2, 6.5), 0);
    assert_eq!(opencv_mat_get(matrix.as_ref().expect("matrix"), 0, 0), 3.5);
    assert_eq!(opencv_mat_get(matrix.as_ref().expect("matrix"), 1, 2), 6.5);
    assert_eq!(opencv_mat_sum(matrix.as_ref().expect("matrix")), 10.0);
    println!("opencv rust bridge: OK");
}
