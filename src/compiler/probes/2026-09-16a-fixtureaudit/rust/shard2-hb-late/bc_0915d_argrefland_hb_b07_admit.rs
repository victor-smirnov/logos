// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b07_admit.logos
struct D { v: i64 }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let o: Option<D> = Some(D { v: 6 });
    match o {
        Some(ref rd) => { if inner(&rd) != 6 { return 7; } }
        None => { return 70; }
    }
    0
}
fn main() { std::process::exit(run()); }
