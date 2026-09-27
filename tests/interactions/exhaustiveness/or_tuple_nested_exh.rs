// exhaustiveness oracle battery (ADR 0030 S3): (bool,bool) {(true|false, true), (_, false)}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let t = (true, false); let r: i32 = match t { (true | false, true) => 1i32, (_, false) => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
