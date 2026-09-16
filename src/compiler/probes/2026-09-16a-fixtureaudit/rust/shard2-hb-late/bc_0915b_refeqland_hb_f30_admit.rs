// TWIN: Eq bound -> PartialEq
fn same<T: PartialEq>(a: &T, b: &T) -> bool { a == b }
fn run() -> i32 {
    let a: usize = 7; let b: usize = 7;
    if !same::<usize>(&a, &b) { return 1; }
    let c: isize = -3; let d: isize = -3;
    if !same::<isize>(&c, &d) { return 2; }
    let e: bool = true; let f: bool = true;
    if !same::<bool>(&e, &f) { return 3; }
    let g: u8 = 200; let h: u8 = 201;
    if same::<u8>(&g, &h) { return 4; }
    0
}
fn main() { std::process::exit(run()); }
