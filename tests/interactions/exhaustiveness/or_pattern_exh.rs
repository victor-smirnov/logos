// exhaustiveness oracle battery (ADR 0030 S3): Color {R|G, B}
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

fn run() -> i32 { let c = Color::G; let r: i32 = match c { Color::R | Color::G => 1i32, Color::B => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
