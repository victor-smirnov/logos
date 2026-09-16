fn f(x: &i64) -> (&i64, i64) {
    return (x, 0i64);
}
fn main() { let z: i64 = 7i64; let t: (&i64, i64) = f(&z); std::process::exit(*t.0 as i32); }
