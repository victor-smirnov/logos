// CONTROL: g11's FIRST claim alone — `&mut T == &T`.
fn run() -> i32 {
    let mut a: (bool, u8, f32) = (true, 3, 1.5);
    let b: (bool, u8, f32) = (true, 3, 1.5);
    let ra: &mut (bool, u8, f32) = &mut a;
    let rb: &(bool, u8, f32) = &b;
    if !(ra == rb) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
