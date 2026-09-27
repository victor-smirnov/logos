// exhaustiveness oracle battery (ADR 0030 S3): i8 {MIN..=-2, 0..=MAX} (-1 missing, value=-1)
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i8 = -1i8; let r: i32 = match x { i8::MIN..=-2i8 => 1i32, 0i8..=i8::MAX => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
