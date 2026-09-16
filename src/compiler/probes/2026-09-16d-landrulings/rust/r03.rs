fn main() {
    let mut a: (bool, u8) = (true, 3u8);
    let b: (bool, u8) = (true, 3u8);
    let ra: &mut (bool, u8) = &mut a;
    let rb: &(bool, u8) = &b;
    std::process::exit(if ra == rb { 0 } else { 1 });
}
