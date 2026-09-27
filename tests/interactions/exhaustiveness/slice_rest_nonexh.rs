// exhaustiveness oracle battery (ADR 0030 S3): &[i64] {[a,b,rest@..],[]} len-1 missing (value len 1)
#![allow(dead_code, unused_variables, unused_assignments)]
fn f(v: &[i64]) -> i64 { match v { [a, b, rest @ ..] => *a + *b + rest.len() as i64, [] => 0i64 } }
fn run() -> i32 { let a: [i64; 4] = [1i64, 2i64, 3i64, 4i64]; return f(&a[0..1]) as i32; }

fn main() { std::process::exit(run()); }
