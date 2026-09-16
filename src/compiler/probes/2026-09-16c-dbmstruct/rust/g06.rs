#![allow(dead_code, unused_variables, unused_mut, static_mut_refs)]

fn run() -> i32 {
    let mut a: (bool, u8, f32) = (true, 3, 1.5);
    let mut b: (bool, u8, f32) = (true, 3, 1.5);
    let ra: &mut (bool, u8, f32) = &mut a;
    let rb: &mut (bool, u8, f32) = &mut b;
    if ra < rb { return 2; }
    return 0;
}
fn main() { std::process::exit(run()); }
