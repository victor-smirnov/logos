fn get(r: &Option<i64>) -> i64 {
    let &Some(x) = r else { return 77; };
    return x;
}
fn main() {
    let o: Option<i64> = Some(5);
    if get(&o) != 5 { std::process::exit(1); }
}
