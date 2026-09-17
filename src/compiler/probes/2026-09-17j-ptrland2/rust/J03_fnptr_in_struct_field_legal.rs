struct H<'r> { f: fn(&'r i64) -> i64 }
fn run<'r>(h: H<'r>, x: &'r i64) -> i64 { let g: fn(&'r i64) -> i64 = h.f; g(x) }
fn rd(x: &i64) -> i64 { *x }
fn logos_main() -> i32 { let v: i64 = 6; (run(H { f: rd }, &v) - 6) as i32 }
fn main() { std::process::exit(logos_main()); }
