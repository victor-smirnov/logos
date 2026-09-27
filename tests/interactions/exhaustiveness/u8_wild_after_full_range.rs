// exhaustiveness oracle battery (ADR 0030 S3): u8 {0..=255, _} wildcard unreachable after full range (warning)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: u8 = 9u8; let r: i32 = match x { 0u8..=255u8 => 1i32, _ => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
