// exhaustiveness oracle battery (ADR 0030 S3): (bool,bool) {(t,t),(f,f)} (value=(f,t))
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let t = (false, true); let r: i32 = match t { (true, true) => 1i32, (false, false) => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
