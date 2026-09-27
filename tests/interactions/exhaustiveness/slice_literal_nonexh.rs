// exhaustiveness oracle battery (ADR 0030 S3): &[i64] {[1,..],[]} non-1 head missing (value [5])
#![allow(dead_code, unused_variables, unused_assignments)]
fn f(v: &[i64]) -> i64 { match v { [1i64, ..] => 1i64, [] => 0i64 } }
fn run() -> i32 { let a: [i64; 1] = [5i64]; return f(&a) as i32; }

fn main() { std::process::exit(run()); }
