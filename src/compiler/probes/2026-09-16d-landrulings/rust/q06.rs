enum E { V { f: i64, g: i64 }, Z }
fn run() -> i64 {
    let p: E = E::V { f: 1i64, g: 9i64 };
    let mut out: i64 = 0i64;
    match &p { E::V { f: 1i64, g } => { out = *g; }, E::V { f: _, g: _ } => { out = 100i64; }, E::Z => {} }
    out
}
fn main() { std::process::exit(if run() != 9i64 { 1 } else { 0 }); }
