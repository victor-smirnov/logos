fn run() -> i32 {
    let a: (bool, u8, f32) = (true, 3, 1.5);
    let b: (bool, u8, f32) = (true, 3, 1.5);
    let c: (bool, u8, f32) = (true, 4, 0.5);
    let ra: &(bool, u8, f32) = &a; let rb: &(bool, u8, f32) = &b; let rc: &(bool, u8, f32) = &c;
    if !(ra == rb) { return 1; }
    if ra < rb { return 2; }
    if !(ra < rc) { return 3; }
    if rc <= ra { return 4; }
    0
}
fn main() { std::process::exit(run()); }
