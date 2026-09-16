fn run() -> i32 {
    let t1: (i64, i64) = (5, 6); let t2: (i64, i64) = (5, 7);
    let r1: &(i64, i64) = &t1; let r2: &(i64, i64) = &t2;
    let x: &&(i64, i64) = &r1; let y: &&(i64, i64) = &r2;
    if x == y { return 1; }
    if !(x != y) { return 2; }
    let a1: [i64; 3] = [1, 2, 3]; let a2: [i64; 3] = [1, 2, 3];
    let q1: &[i64; 3] = &a1; let q2: &[i64; 3] = &a2;
    let u: &&[i64; 3] = &q1; let w: &&[i64; 3] = &q2;
    if u != w { return 3; }
    0
}
fn main() { std::process::exit(run()); }
