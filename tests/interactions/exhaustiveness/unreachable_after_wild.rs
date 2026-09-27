// exhaustiveness oracle battery (ADR 0030 S3): Color {_, R} arm after wildcard (warning)
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

fn run() -> i32 { let c = Color::R; let r: i32 = match c { _ => 1i32, Color::R => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
