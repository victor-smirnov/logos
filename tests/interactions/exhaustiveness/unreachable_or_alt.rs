// exhaustiveness oracle battery (ADR 0030 S3): Option<bool> {Some(true), Some(true|false), None} partly-unreachable alt (warning)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let o: Option<bool> = Option::Some(false); let r: i32 = match o { Option::Some(true) => 1i32, Option::Some(true | false) => 2i32, Option::None => 3i32 }; return r; }

fn main() { std::process::exit(run()); }
