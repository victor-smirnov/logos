fn main() {
    let x: f64 = 1.5; let y: f64 = 2.5;
    let rx: &f64 = &x; let ry: &f64 = &y;
    let lt: bool = rx < ry;
    println!("lt={}", lt as i32);
    if !lt { std::process::exit(1); }
}
