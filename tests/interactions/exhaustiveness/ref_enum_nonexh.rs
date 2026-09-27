// exhaustiveness oracle battery (ADR 0030 S3): match &Color missing B (value=B)
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

fn run() -> i32 { let c = Color::B; let r: i32 = match &c { Color::R => 1i32, Color::G => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
