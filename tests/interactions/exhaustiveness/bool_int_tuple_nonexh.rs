// exhaustiveness oracle battery (ADR 0030 S3): (bool,i64) {(t,_),(f,0)} (value=(f,3))
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let t = (false, 3i64); let r: i32 = match t { (true, _) => 1i32, (false, 0i64) => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
