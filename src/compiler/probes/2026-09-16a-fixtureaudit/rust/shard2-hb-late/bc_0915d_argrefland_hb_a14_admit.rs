// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a14_admit.logos
fn inner(a: &&String) -> i64 { let x: &String = *a; x.len() as i64 }
fn run() -> i32 {
    let s: String = String::from("abcdef");
    let rs: &String = &s;
    if inner(&rs) != 6 { return 14; }
    0
}
fn main() { std::process::exit(run()); }
