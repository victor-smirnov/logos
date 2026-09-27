// exhaustiveness oracle battery (ADR 0030 S3): i8 {-128..=-1 | 0..=127} one or-arm, literal bounds
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i8 = 5i8; let r: i32 = match x { -128i8..=-1i8 | 0i8..=127i8 => 3i32 }; return r; }

fn main() { std::process::exit(run()); }
