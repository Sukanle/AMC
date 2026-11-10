//! Consumer crate for the AMC Rust workspace example.
//!
//! `amc generate` writes `generated/amc_generated.rs`; this binary uses only
//! the generated safe facade, so the whole consumer compiles without unsafe.

#![deny(unsafe_code)]

#[path = "generated/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}

use skl::abix::safe::*;

fn main() {
    let widget = widget_make(7).expect("widget");
    assert_eq!(widget_live(), 1);
    assert_eq!(widget_value(widget), 7);
    assert_eq!(widget_live(), 0);
    println!("amc ffi workspace: OK");
}
