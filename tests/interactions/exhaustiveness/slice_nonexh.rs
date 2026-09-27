// exhaustiveness oracle battery (ADR 0030 S3): &[i64] {[],[x]} longer missing (value len 3)
#![allow(dead_code, unused_variables, unused_assignments)]
fn f(v: &[i64]) -> i64 { match v { [] => 0i64, [x] => *x } }
fn run() -> i32 { let a: [i64; 3] = [1i64, 2i64, 3i64]; return f(&a) as i32; }

fn main() { std::process::exit(run()); }
