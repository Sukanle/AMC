#[path = "gen/amc_generated.rs"]
pub mod abix;

mod skl {
    pub use crate::abix;
}
use skl::abix::*;

fn main() {
    let mut counter = OwnedCounter::create(40).expect("create");
    assert_eq!(counter.get(), 40);
    counter.add(2);
    assert_eq!(counter.get(), 42);
    println!("expose counter: OK");
}
