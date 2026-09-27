fn pick(flag: bool) -> impl std::fmt::Debug + Clone { if flag { return 1i32; } return 2i32; }
fn main() { println!("{:?}", pick(true).clone()); }
