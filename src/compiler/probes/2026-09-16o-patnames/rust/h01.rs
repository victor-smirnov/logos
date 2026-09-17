struct P { x: i64, y: i64 }
fn main() {
    let sv: (P, i64) = (P { x: 3, y: 2 }, 2);
    match sv {
        (P { x, y }, z) => { if x + y + z != 7 { std::process::exit(1); } }
    }
}
