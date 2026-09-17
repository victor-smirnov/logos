struct H<'r> { f: fn(&'r i64) -> &'r i64 }
fn run<'r>(h: H<'r>, x: &'r i64) -> &'r i64 { let g: fn(&'r i64) -> &'r i64 = h.f; g(x) }
fn id(x: &i64) -> &i64 { x }
fn logos_main() -> i32 { let out: &i64; { let v: i64 = 3; out = run(H { f: id }, &v); } *out as i32 }
fn main() { std::process::exit(logos_main()); }
