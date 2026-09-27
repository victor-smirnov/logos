// exhaustiveness oracle battery (ADR 0030 S3): u8 {0..=10, 5..=8, _} subsumed range (warning)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: u8 = 6u8; let r: i32 = match x { 0u8..=10u8 => 1i32, 5u8..=8u8 => 2i32, _ => 3i32 }; return r; }

fn main() { std::process::exit(run()); }
