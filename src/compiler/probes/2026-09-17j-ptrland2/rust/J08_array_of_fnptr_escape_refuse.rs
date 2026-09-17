fn apply<'r>(gs: [fn(&'r i64) -> &'r i64; 1], x: &'r i64) -> &'r i64 { gs[0](x) }
fn id(x: &i64) -> &i64 { x }
fn logos_main() -> i32 { let out: &i64; { let v: i64 = 3; let gs: [fn(&i64) -> &i64; 1] = [id]; out = apply(gs, &v); } *out as i32 }
fn main() { std::process::exit(logos_main()); }
