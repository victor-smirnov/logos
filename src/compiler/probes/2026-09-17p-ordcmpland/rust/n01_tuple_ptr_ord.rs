fn main() {
    let arr: [i64; 4] = [1, 2, 3, 4];
    let lo: *const i64 = &arr[0] as *const i64;
    let hi: *const i64 = &arr[3] as *const i64;
    let t1: (*const i64, i64) = (lo, 1);
    let t2: (*const i64, i64) = (hi, 1);
    let lt = t1 < t2;
    println!("lt={}", lt as i32);
    if !lt { std::process::exit(1); }
}
