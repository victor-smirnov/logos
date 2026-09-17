fn cmp2<T: Ord>(a: &T, b: &T) -> bool { a < b }
fn main() {
    let big: i64 = 100;
    let small: i64 = 1;
    let lt: bool = cmp2::<i64>(&big, &small);
    println!("lt={}", lt as i32);
    if lt { std::process::exit(1); }
}
