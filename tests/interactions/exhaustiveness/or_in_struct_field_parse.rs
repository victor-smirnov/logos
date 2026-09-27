// exhaustiveness oracle battery (ADR 0030 S3): struct pattern with or-pattern in a field S{c: R|G, ..}
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

struct S { c: Color, b: bool }
fn run() -> i32 { let s = S { c: Color::G, b: false }; let r: i32 = match s { S { c: Color::R | Color::G, .. } => 1i32, S { c: Color::B, .. } => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
