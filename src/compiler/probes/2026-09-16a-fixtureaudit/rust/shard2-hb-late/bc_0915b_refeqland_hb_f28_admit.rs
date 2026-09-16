fn run() -> i32 {
    let a: [i64; 2] = [1, 2];
    if &a != &[1, 2] { return 1; }
    if &a == &[2, 1] { return 2; }
    0
}
fn main() { std::process::exit(run()); }
