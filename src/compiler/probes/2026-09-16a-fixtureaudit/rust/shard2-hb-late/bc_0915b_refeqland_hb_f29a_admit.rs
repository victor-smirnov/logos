// TWIN: Eq bound -> PartialEq
fn same<T: PartialEq>(a: &&T, b: &&T) -> bool { a == b }
fn run() -> i32 {
    let a: i64 = 6; let b: i64 = 6;
    let ra: &i64 = &a; let rb: &i64 = &b;
    if !same::<i64>(&ra, &rb) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
