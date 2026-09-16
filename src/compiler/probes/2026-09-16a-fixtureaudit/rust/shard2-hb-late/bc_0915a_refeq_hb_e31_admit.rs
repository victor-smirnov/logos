// TWIN of bc_0915a_refeq_hb_e31_admit.logos ; TWIN: Eq bound -> PartialEq
fn same<T: PartialEq>(a: &T, b: &T) -> bool { a == b }
fn run() -> i32 {
    let a: char = 'k'; let b: char = 'k';
    if !same::<char>(&a, &b) { return 1; }
    0
}
fn main() { std::process::exit(run()); }
