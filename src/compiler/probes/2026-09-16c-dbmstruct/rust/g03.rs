#![allow(dead_code, unused_variables, unused_mut, static_mut_refs)]

fn run() -> i32 {
    let mut a: i64 = 1;
    let b: i64 = 2;
    let ra: &mut i64 = &mut a;
    let rb: &i64 = &b;
    if !(ra < rb) { return 2; }
    return 0;
}
fn main() { std::process::exit(run()); }
