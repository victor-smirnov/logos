struct P { x: String }
fn main() {
    let p = P { x: String::from("hi") };
    let r: &P = &p; let pp: &&P = &r; let k: i32 = 1;
    match (pp, k) { (&&P { x }, _j) => { println!("{}", x); } }
}
