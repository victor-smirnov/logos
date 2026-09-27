// exhaustiveness oracle battery (ADR 0030 S3): [i64;3] {[x, ..]} irrefutable with rest
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let a: [i64; 3] = [6i64, 2i64, 3i64]; let r: i64 = match a { [x, ..] => x }; return r as i32; }

fn main() { std::process::exit(run()); }
