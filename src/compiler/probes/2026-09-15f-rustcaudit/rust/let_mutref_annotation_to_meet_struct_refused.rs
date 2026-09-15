struct P<'a> { x: &'a i64, y: &'a i64 }
fn bump(p: &mut P) -> i64 { return *p.x + *p.y; }
fn go<'a, 'b>(x: &'a i64, y: &'b i64) -> i64 {
    let mut p = P { x: x, y: y };
    let r: &mut P = &mut p;
    return bump(r);
}
fn logos_main() -> i32 {
    let a: i64 = 2i64;
    let b: i64 = 7i64;
    return go(&a, &b) as i32;
}

fn main() { std::process::exit(logos_main()); }
