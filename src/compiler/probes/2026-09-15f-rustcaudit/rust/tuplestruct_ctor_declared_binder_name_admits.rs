struct P<'s>(&'s i64, &'s i64);
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> P<'a> {
    return P(x, y);
}
fn main() {
    let a: i64 = 2i64;
    let b: i64 = 5i64;
    let p: P = mk(&a, &b);
    std::process::exit((*p.0 + *p.1) as i32);
}
