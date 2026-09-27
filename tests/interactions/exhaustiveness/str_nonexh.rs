// exhaustiveness oracle battery (ADR 0030 S3): &str {"a","b"} no wildcard (value="c")
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let s: &str = "c"; let r: i32 = match s { "a" => 1i32, "b" => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
