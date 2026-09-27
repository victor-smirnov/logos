// exhaustiveness oracle battery (ADR 0030 S3): i8 {MIN..=-1, 0..=MAX}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let x: i8 = -5i8; let r: i32 = match x { i8::MIN..=-1i8 => 1i32, 0i8..=i8::MAX => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
