fn main() {
    let mut a: (bool, u8) = (true, 3u8);
    let mut b: (bool, u8) = (true, 4u8);
    let ra: &mut (bool, u8) = &mut a;
    let rb: &mut (bool, u8) = &mut b;
    std::process::exit(if ra < rb { 0 } else { 1 });
}
