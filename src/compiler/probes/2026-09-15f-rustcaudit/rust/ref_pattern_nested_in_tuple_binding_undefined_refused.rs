struct P { x: i32 }
fn logos_main() -> i32 {
    let p = P { x: 5i32 };
    let r: &P = &p;
    let pp: &&P = &r;
    let k: i32 = 1i32;
    match (pp, k) {
        (&&P { x }, j) => { if x + j != 6i32 { return 1i32; } }
    }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
