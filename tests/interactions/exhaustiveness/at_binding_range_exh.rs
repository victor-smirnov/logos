// exhaustiveness oracle battery (ADR 0030 S3): u8 {n @ 0..=9, m @ 10..=255}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: u8 = 255u8; let r: i32 = match x { n @ 0u8..=9u8 => n as i32, m @ 10u8..=255u8 => (m / 5u8) as i32 }; return r; }

fn main() { std::process::exit(run()); }
