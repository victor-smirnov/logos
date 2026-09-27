// exhaustiveness oracle battery (ADR 0030 S3): Color {R, R, G, B} duplicate arm (warning)
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

fn run() -> i32 { let c = Color::R; let r: i32 = match c { Color::R => 1i32, Color::R => 2i32, Color::G => 3i32, Color::B => 4i32 }; return r; }

fn main() { std::process::exit(run()); }
