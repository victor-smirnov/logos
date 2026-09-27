// exhaustiveness oracle battery (ADR 0030 S3): char {'\0'..='\u{D7FF}', '\u{E000}'..='\u{10FFFF}'}
#![allow(dead_code, unused_variables, unused_assignments)]
fn run() -> i32 { let c: char = 'x'; let r: i32 = match c { '\0'..='\u{D7FF}' => 1i32, '\u{E000}'..='\u{10FFFF}' => 2i32 }; return r; }

fn main() { std::process::exit(run()); }
