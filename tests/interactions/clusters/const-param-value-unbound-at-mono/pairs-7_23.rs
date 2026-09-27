fn pick<const N: usize>() -> usize { return N * 2; }
fn main() { println!("{} {}", pick::<4>(), pick::<2>()); }
