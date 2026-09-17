fn main() {
    let a: [u8; 4] = [1, 2, 3, 4];
    let p: *const u8 = &a[0] as *const u8;
    let q: *const u8 = &a[3] as *const u8;
    let lt: bool = p < q;
    println!("lt={}", lt as i32);
    if !lt { std::process::exit(1); }
}
