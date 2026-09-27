// exhaustiveness oracle battery (ADR 0030 S3): u8 {n @ 0..=9, 10..=254} (255 missing, value=255)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: u8 = 255u8; let r: i32 = match x { n @ 0u8..=9u8 => n as i32, 10u8..=254u8 => 50i32 }; return r; }

fn main() { std::process::exit(run()); }
