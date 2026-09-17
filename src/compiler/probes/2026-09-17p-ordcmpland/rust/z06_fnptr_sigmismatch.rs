fn f1() -> i64 { 1 }
fn f2(x: i64) -> i64 { x }
fn main() { let a: fn() -> i64 = f1; let b: fn(i64) -> i64 = f2;
    if a < b { std::process::exit(1); } }
