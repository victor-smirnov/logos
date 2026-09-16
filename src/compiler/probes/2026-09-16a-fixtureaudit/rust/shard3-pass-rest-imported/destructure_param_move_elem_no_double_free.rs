fn probe((s, b): (String, i64)) -> i64 {
    return (s.len() as i64) + b;
}
fn main() {
    let t: (String, i64) = (String::from("abcde"), 2i64);
    if probe(t) != 7i64 { std::process::exit(1); }
    std::process::exit(0);
}
