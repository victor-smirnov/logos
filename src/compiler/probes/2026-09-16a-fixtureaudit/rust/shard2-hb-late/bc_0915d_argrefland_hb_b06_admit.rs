// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b06_admit.logos
struct D { v: i64 }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let o: Option<D> = Some(D { v: 6 });
    if let Some(rd) = &o {
        if inner(&rd) != 6 { return 6; }
        return 0;
    }
    60
}
fn main() { std::process::exit(run()); }
