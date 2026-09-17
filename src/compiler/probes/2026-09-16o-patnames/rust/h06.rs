fn get(o: Option<i64>) -> i64 {
    let Some(w @ 1..=9) = o else { return 77; };
    return w;
}
fn main() {
    let o: Option<i64> = Some(5);
    if get(o) != 5 { std::process::exit(1); }
}
