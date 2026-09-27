fn len(s: &str) -> i64 { s.len() as i64 }
fn main() {
    let words = ["ab", "cde"];
    let mut t = 0i64;
    for w in words.iter() { t += len(w); }
    let mut best: &str = "";
    for w in words.iter() { if w.len() > best.len() { best = w; } }
    println!("{} {}", t, best);
}
