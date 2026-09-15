enum E<'a> { N, Two(&'a i64, &'a i64) }
fn second<'a, 'b>(x: &'a i64, e: E<'b>) -> &'a i64 {
    match e {
        E::Two(_, q) => { return q; }
        E::N => { return x; }
    }
}
fn main() {
    let a: i64 = 3i64;
    let b: i64 = 4i64;
    std::process::exit(*second(&a, E::Two(&b, &b)) as i32);
}
