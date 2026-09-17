fn main() {
    let x: i64 = 9; let y: i64 = 2;
    let rx: &i64 = &x; let ry: &i64 = &y;
    let px: &&i64 = &rx; let py: &&i64 = &ry;
    let lt: bool = px < py;
    println!("lt={}", lt as i32);
    if lt { std::process::exit(1); }
}
