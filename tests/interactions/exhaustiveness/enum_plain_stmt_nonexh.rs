// exhaustiveness oracle battery (ADR 0030 S3): enum stmt-form match missing B (value=B), fallthrough return 99
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

fn run() -> i32 { let c = Color::B; match c { Color::R => { return 1i32; } Color::G => { return 2i32; } } return 99i32; }

fn main() { std::process::exit(run()); }
