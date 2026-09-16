struct S { n: i64 }
impl Drop for S { fn drop(&mut self) { println!("D{}", self.n); } }
enum E { V { f: S }, Z }
fn run() -> i64 {
    let p: E = E::V { f: S { n: 4i64 } };
    let mut out: i64 = 0i64;
    match &p { E::V { f: _f } => { out = 4i64; }, E::Z => {} }
    out
}
fn main() { std::process::exit(if run() != 4i64 { 1 } else { 0 }); }
