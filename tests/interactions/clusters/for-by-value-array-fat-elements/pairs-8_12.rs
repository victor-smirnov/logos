


enum Cmd { Push(i64), Pop }
fn main() {
 let cs = [Some(1i64), Some(2), None]; let mut n = 0i64; for c in cs { if let Some(v) = c { n += v; } } println!("{}", n);
}
