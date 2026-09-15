struct N { a: i64 }
impl Drop for N { fn drop(self: &mut N) {} }
fn main() {
    let mut x: N = N { a: 1i64 };
    let y: &mut N = &mut x;
    y.a = 2i64;
    let z = x;
    let _ = z.a;
    y.a = 3i64;
    std::process::exit(0);
}
