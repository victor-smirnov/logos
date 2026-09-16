// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a20_admit.logos
struct D { v: i64 }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let o: Option<D> = Some(D { v: 6 });
    match &o {
        Some(rd) => { if inner(&rd) != 6 { return 20; } }
        None => { return 21; }
    }
    0
}
fn main() { std::process::exit(run()); }
