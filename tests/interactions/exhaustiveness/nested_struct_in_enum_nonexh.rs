// exhaustiveness oracle battery (ADR 0030 S3): enum E{A(S{b:bool})} only A(S{b:true}) (value=false)
#![allow(dead_code, unused_variables, unused_assignments)]
struct S { b: bool, n: i64 }
enum E { A(S), Z }
fn run() -> i32 { let e = E::A(S { b: false, n: 3i64 }); let r: i64 = match e { E::A(S { b: true, n }) => n, E::Z => 0i64 }; return r as i32; }

fn main() { std::process::exit(run()); }
