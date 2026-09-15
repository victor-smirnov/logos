#[derive(Clone, Copy)]
struct W<'a, 'b> where 'b: 'a { lo: &'a i64, hi: &'b i64 }
fn go<'a, 'c>(x: &'a i64, z: &'c i64) -> i64 {
    let w = W { lo: x, hi: z };
    return *w.lo + *w.hi;
}
fn logos_main() -> i32 {
    let a: i64 = 1i64;
    let c: i64 = 3i64;
    return go(&a, &c) as i32;
}

fn main() { std::process::exit(logos_main()); }
