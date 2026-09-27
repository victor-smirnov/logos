// exhaustiveness oracle battery (ADR 0030 S3): &[i64] {[],[x],[x,y,..]}
#![allow(dead_code, unused_variables, unused_assignments)]
fn f(v: &[i64]) -> i64 { match v { [] => 0i64, [x] => *x, [x, y, ..] => *x + *y } }
fn run() -> i32 { let a: [i64; 3] = [1i64, 2i64, 3i64]; return (f(&a) + f(&a[0..1]) * 10i64 + f(&a[0..0])) as i32; }

fn main() { std::process::exit(run()); }
