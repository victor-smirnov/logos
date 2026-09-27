// exhaustiveness oracle battery (ADR 0030 S3): let 0..=255u8 = x; (full-domain range, irrefutable)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: u8 = 7u8; let 0u8..=255u8 = x; return 3i32; }

fn main() { std::process::exit(run()); }
