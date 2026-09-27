// exhaustiveness oracle battery (ADR 0030 S3): u8 {0..=100, 102..=255} gap at 101 (value=101)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: u8 = 101u8; let r: i32 = match x { 0u8..=100u8 => 1i32, 102u8..=255u8 => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
