// exhaustiveness oracle battery (ADR 0030 S3): let (a,b) = t else {..}; irrefutable (rustc warns)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let t = (2i64, 3i64); let (a, b) = t else { return 11i32; }; return (a + b) as i32; }

fn main() { std::process::exit(run()); }
