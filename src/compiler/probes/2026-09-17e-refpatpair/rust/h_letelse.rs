fn get(r: &Option<i64>) -> i64 { let &Option::Some(x) = r else { return 77; }; x }
fn main() {
    let o: Option<i64> = Some(5); let n: Option<i64> = None;
    println!("got={} {}", get(&o), get(&n));
    std::process::exit(if get(&o) != 5 || get(&n) != 77 {1} else {0});
}
