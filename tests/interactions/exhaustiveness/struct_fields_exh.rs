// exhaustiveness oracle battery (ADR 0030 S3): S{c:Color,b:bool} covered by 3 arms
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

struct S { c: Color, b: bool }
fn run() -> i32 { let s = S { c: Color::G, b: false }; let r: i32 = match s { S { c: Color::R, .. } => 1i32, S { b: true, .. } => 2i32, S { c: Color::G | Color::B, b: false } => 3i32 }; return r; }

fn main() { std::process::exit(run()); }
