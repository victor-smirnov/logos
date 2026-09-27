// exhaustiveness oracle battery (ADR 0030 S3): String.as_str() {"a"} no wildcard (value="z")
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let s: String = String::from("z"); let r: i32 = match s.as_str() { "a" => 1i32 }; return r; }

fn main() { std::process::exit(run()); }
