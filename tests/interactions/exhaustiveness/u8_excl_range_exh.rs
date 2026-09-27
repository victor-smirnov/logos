// exhaustiveness oracle battery (ADR 0030 S3): u8 {0..128, 128..=255} (exclusive range)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: u8 = 128u8; let r: i32 = match x { 0u8..128u8 => 1i32, 128u8..=255u8 => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
