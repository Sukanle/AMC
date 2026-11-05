#[path = "gen/amc_generated.rs"]
mod abi;
use abi::*;

fn main() {
    let mut counter = OwnedCounter::create(40).expect("create");
    assert_eq!(counter.get(), 40);
    counter.add(2);
    assert_eq!(counter.get(), 42);
    println!("expose counter: OK");
}
