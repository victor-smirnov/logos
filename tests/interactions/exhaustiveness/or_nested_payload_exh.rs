// exhaustiveness oracle battery (ADR 0030 S3): Option<Color> {Some(R|G),Some(B),None}
#![allow(dead_code, unused_variables, unused_assignments)]
enum Color { R, G, B }

fn run() -> i32 { let o: Option<Color> = Option::Some(Color::B); let r: i32 = match o { Option::Some(Color::R | Color::G) => 1i32, Option::Some(Color::B) => 2i32, Option::None => 3i32 }; return r; }

fn main() { std::process::exit(run()); }
