// exhaustiveness oracle battery (ADR 0030 S3): i8 {-128..=-2, 0..=127} (-1 missing, value=-1)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i8 = -1i8; let r: i32 = match x { -128i8..=-2i8 => 1i32, 0i8..=127i8 => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
