struct P { x: i64, y: i64 }
fn main() {
    let sv: (P, i64) = (P { x: 1, y: 2 }, 9);
    let t;
    match sv {
        (P { x, y }, 9) | (P { x, y }, 8) => { t = x + y; }
        _ => { t = 100; }
    }
    if t != 3 { std::process::exit(1); }
    std::process::exit(0);
}
