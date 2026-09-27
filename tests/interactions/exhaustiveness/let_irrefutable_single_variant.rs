// exhaustiveness oracle battery (ADR 0030 S3): enum W{V(i64)}; let W::V(x) = w;
#![allow(dead_code, unused_variables, unused_assignments)]
enum W { V(i64) }
fn run() -> i32 { let w = W::V(9i64); let W::V(x) = w; return x as i32; }

fn main() { std::process::exit(run()); }
