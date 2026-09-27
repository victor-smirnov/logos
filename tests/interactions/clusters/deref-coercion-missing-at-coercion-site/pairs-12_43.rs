fn l(s: &str) -> i64 { return s.len() as i64; }
fn main() {
    let a: &str = "abc"; let ra = &a;
    println!("{}", l(ra));
    let keys: [&str; 2] = ["a", "bb"];
    let t: i64 = keys.iter().map(|k| l(k)).sum();
    println!("{}", t);
}
