struct P { x: i64 }
fn main() {
    let p = P { x: 5 };
    let r: &P = &p;
    let k: i64 = 1;
    match (r, k) {
        (&P { x }, j) => { if x + j != 6 { std::process::exit(1); } }
    }
}
