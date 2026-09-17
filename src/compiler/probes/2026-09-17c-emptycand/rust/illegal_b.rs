static V: i64 = 5;
fn pick<'a>(x: &'a i64, y: &'a i64) -> &'a i64 { if *x > *y { x } else { y } }
fn escape() -> &'static i64 {
    let n: i64 = 9;
    pick(&V, &n)          // must be E0597: n does not live long enough
}
fn main() { println!("{}", escape()); }
