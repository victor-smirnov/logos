// exhaustiveness oracle battery (ADR 0030 S3): char {'a'..='z','A'..='Z'} (value='0')
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let c: char = '0'; let r: i32 = match c { 'a'..='z' => 1i32, 'A'..='Z' => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
