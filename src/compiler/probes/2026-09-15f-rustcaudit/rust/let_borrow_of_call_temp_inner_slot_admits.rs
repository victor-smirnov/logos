fn id<'q>(x: &'q i64) -> &'q i64 {
    return x;
}
fn foo<'a>(p: &'a i64) {
    let z: &'a &i64 = &(id(p));
}
fn main() {}
