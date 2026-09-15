fn ida(x: &i64) -> i64 { return *x; }
fn t<'r>(g: fn(&'r i64) -> i64) -> i32 {
    let mut f: fn(&i64) -> i64 = ida;
    f = g;
    return 0i32;
}
fn main() {}
