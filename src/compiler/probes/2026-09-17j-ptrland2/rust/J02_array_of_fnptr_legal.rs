fn apply<'r>(gs: [fn(&'r i64) -> i64; 2], x: &'r i64) -> i64 { gs[0](x) + gs[1](x) }
fn rd(x: &i64) -> i64 { *x }
fn logos_main() -> i32 { let v: i64 = 5; let gs: [fn(&i64) -> i64; 2] = [rd, rd]; (apply(gs, &v) - 10) as i32 }
fn main() { std::process::exit(logos_main()); }
