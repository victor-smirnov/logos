// exhaustiveness oracle battery (ADR 0030 S3): (i64,i64) {(0,_),(_,0)} (value=(1,1))
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let t = (1i64, 1i64); let r: i32 = match t { (0i64, _) => 1i32, (_, 0i64) => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
