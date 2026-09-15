fn store<'a>(out: &mut &'a i64) {
    let bx: Box<i64> = Box::new(3i64);
    *out = &*bx;
}
fn main() {}
