fn foo<'a>(out: &mut &'a i64) {
    let v: i64 = 1i64;
    let r: &i64 = &v;
    *out = r;
}
fn main() {}
