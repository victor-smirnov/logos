fn main() {
    let hi: [i64; 2] = [9, 9];
    let lo: [i64; 2] = [1, 1];
    let a: &[i64] = &hi;
    let b: &[i64] = &lo;
    let lt: bool = a < b;
    println!("lt={}", lt as i32);
    if lt { std::process::exit(1); }
}
