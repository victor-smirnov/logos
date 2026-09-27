// exhaustiveness oracle battery (ADR 0030 S3): &[i64] {[first, ..]} empty missing (value [])
#![allow(dead_code, unused_variables, unused_assignments)]
fn f(v: &[i64]) -> i64 { match v { [first, ..] => *first } }
fn run() -> i32 { let a: [i64; 3] = [4i64, 2i64, 3i64]; return f(&a[0..0]) as i32; }

fn main() { std::process::exit(run()); }
