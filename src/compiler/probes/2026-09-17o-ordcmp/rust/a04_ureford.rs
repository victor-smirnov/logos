fn main() {
    let x: u64 = 18446744073709551615; let y: u64 = 1;
    let rx: &u64 = &x; let ry: &u64 = &y;
    let lt: bool = rx < ry;
    println!("lt={}", lt as i32);
    if lt { std::process::exit(1); }
}
