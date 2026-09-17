struct P { x: i64, y: i64 }
fn main() {
    let sv: ((P, i64), i64) = ((P { x: 1, y: 2 }, 3), 4);
    match sv {
        ((P { x, y }, q), z) => { if x + y + q + z != 10 { std::process::exit(1); } }
    }
    std::process::exit(0);
}
