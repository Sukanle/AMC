// Explicit container-contract fixture for `[[rust.expose]]` + `[[adapt]]`.
//
// The source functions keep their ordinary Rust signatures. The generated
// wrapper takes the declared ABIX-STD boundary type and marshals it onto the
// native `String`, `Vec<T>` or `Option<T>` value.

#[repr(C)]
pub struct ContainerApi {
    pub value: i32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct ContainerPoint {
    pub x: i32,
    pub y: i32,
}

pub fn container_string_len(value: String) -> usize {
    value.len()
}

pub fn container_vec_sum(value: Vec<i32>) -> i32 {
    value.iter().sum()
}

pub fn container_option_value(value: Option<i32>) -> i32 {
    value.unwrap_or(-1)
}

pub fn container_str_view_len(value: &str) -> usize {
    value.len()
}

pub fn container_slice_sum(value: &[i32]) -> i32 {
    value.iter().sum()
}

pub fn container_make_string(value: i32) -> String {
    value.to_string()
}

pub fn container_points_sum(value: Vec<ContainerPoint>) -> i32 {
    value.iter().map(|point| point.x + point.y).sum()
}

pub fn container_option_point(value: Option<ContainerPoint>) -> i32 {
    value.map(|point| point.x + point.y).unwrap_or(-1)
}

pub fn container_option_make(value: i32) -> Option<i32> {
    if value < 0 {
        None
    } else {
        Some(value)
    }
}

pub fn container_owned_string(value: i32) -> String {
    value.to_string()
}

pub fn container_owned_points(value: i32) -> Vec<ContainerPoint> {
    vec![ContainerPoint { x: value, y: value + 1 }]
}
