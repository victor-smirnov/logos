fn main() {
    let a: (bool, u8) = (true, 3u8);
    let b: (bool, u8) = (true, 4u8);
    let ra: &(bool, u8) = &a;
    let rb: &(bool, u8) = &b;
    std::process::exit(if ra < rb { 0 } else { 1 });
}
