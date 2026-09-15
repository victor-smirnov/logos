struct P { x: i64, y: i64 }
fn bump(r: &mut i64) { *r = *r + 100i64; }
fn main() {
    let p: P = P { x: 1i64, y: 2i64 };
    let mut out: i64 = 0i64;
    match p {
        P { x: a, y } => { bump(&mut a); out = a + y; }
    }
    if out != 103i64 { std::process::exit(1); }
    std::process::exit(0);
}
