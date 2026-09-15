struct A<'a> { x: &'a i64 }
fn foo<'a>(out: &mut A<'a>) {
    let v: i64 = 1i64;
    out.x = &v;
}
fn main() {}
