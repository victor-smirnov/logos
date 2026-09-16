struct S { n: i64 }
impl Drop for S { fn drop(&mut self) { println!("D{}", self.n); } }
enum E { V { f: S }, Z }
fn run() -> i64 { let p: E = E::V { f: S { n: 2i64 } }; match &p { E::V { f } => { return f.n; }, E::Z => {} } 0i64 }
fn main() { std::process::exit(if run() != 2i64 { 1 } else { 0 }); }
