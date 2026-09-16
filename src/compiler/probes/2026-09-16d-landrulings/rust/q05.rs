enum E { V { f: i64, g: i64 }, Z }
fn run() -> i64 {
    let p: E = E::V { f: 5i64, g: 6i64 };
    let mut out: i64 = 0i64;
    match &p { E::V { f, g } => { out = f + g + 1i64; }, E::Z => {} }
    out
}
fn main() { std::process::exit(if run() != 12i64 { 1 } else { 0 }); }
