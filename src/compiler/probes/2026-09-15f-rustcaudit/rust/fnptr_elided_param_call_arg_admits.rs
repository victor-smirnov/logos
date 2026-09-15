fn want(g: fn(&i64) -> i64) -> i64 { return 0i64; }
fn t<'r>(g: fn(&'r i64) -> i64) -> i64 {
    return want(g);
}
fn main() {}
