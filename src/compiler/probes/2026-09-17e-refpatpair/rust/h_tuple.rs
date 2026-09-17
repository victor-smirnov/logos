struct P { x: i32 }
fn main() {
    let p = P { x: 5 }; let r: &P = &p; let pp: &&P = &r; let k: i32 = 1;
    match (pp, k) { (&&P { x }, j) => { println!("got={}", x + j); std::process::exit(if x + j != 6 {1} else {0}); } }
}
