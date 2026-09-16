fn run() -> i32 {
    let z: f64 = 0.0;
    let nan: f64 = z / z;
    let a: (f64, f64) = (nan, 1.0); let b: (f64, f64) = (nan, 1.0);
    let ra: &(f64, f64) = &a; let rb: &(f64, f64) = &b;
    if ra == rb { return 1; }
    let c: (f64, f64) = (-0.0, 1.0); let d: (f64, f64) = (0.0, 1.0);
    let rc: &(f64, f64) = &c; let rd: &(f64, f64) = &d;
    if rc != rd { return 2; }
    0
}
fn main() { std::process::exit(run()); }
