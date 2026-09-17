struct P { x: i32 }
fn main() {
    let p = P { x: 5 };
    let r: &P = &p;
    let pp: &&P = &r;
    let k: i32 = 1;
    let got;
    match (pp, k) {
        (&&P { x }, j) => { got = x + j; }
    }
    println!("got={}", got);
    if got != 6 { std::process::exit(1); }
    std::process::exit(0);
}
