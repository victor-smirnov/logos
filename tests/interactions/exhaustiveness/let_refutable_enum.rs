// exhaustiveness oracle battery (ADR 0030 S3): let Color::R = c; (E0005)
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

fn run() -> i32 { let c = Color::B; let Color::R = c; return 1i32; }

fn main() { std::process::exit(run()); }
