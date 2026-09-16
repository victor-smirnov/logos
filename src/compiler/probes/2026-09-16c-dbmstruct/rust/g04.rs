#![allow(dead_code, unused_variables, unused_mut, static_mut_refs)]

fn run() -> i32 {
    let mut a: [i64; 2] = [1, 2];
    let b: [i64; 2] = [1, 3];
    let ra: &mut [i64; 2] = &mut a;
    let rb: &[i64; 2] = &b;
    if !(ra < rb) { return 2; }
    return 0;
}
fn main() { std::process::exit(run()); }
