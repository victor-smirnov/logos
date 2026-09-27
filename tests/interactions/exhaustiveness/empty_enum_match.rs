// exhaustiveness oracle battery (ADR 0030 S3): fn f(e: Empty) { match e {} }
#![allow(dead_code, unused_variables, unused_assignments)]
enum Empty {}

fn f(e: Empty) -> i32 { match e {} }
fn run() -> i32 { return 4i32; }

fn main() { std::process::exit(run()); }
