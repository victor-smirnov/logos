fn main() {
    let f: bool = false; let t: bool = true;
    let rf: &bool = &f; let rt: &bool = &t;
    let lt: bool = rf < rt;
    println!("lt={}", lt as i32);
    if !lt { std::process::exit(1); }
}
