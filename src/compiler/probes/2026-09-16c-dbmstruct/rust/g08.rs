#![allow(dead_code, unused_variables, unused_mut, static_mut_refs)]

fn run() -> i32 {
    let mut a: u8 = 1;
    let b: u8 = 2;
    let ra: &mut u8 = &mut a;
    let rb: &u8 = &b;
    if !(ra <= rb) { return 2; }
    return 0;
}
fn main() { std::process::exit(run()); }
