struct S { n: i64 }
impl Drop for S { fn drop(&mut self) { println!("D{}", self.n); } }
enum E { V { f: S }, Z }
fn get(x: &E) -> i64 { match x { E::V { f } => f.n, E::Z => 0i64 } }
fn run() -> i64 { let p: E = E::V { f: S { n: 6i64 } }; get(&p) }
fn main() { std::process::exit(if run() != 6i64 { 1 } else { 0 }); }
