// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h05_admit.logos
struct D { v: i64 }
fn bump(a: &&mut D) -> i64 { a.v + 1 }
fn run() -> i32 {
    let mut o: Option<D> = Some(D { v: 6 });
    match &mut o {
        Some(rd) => {
            if bump(&rd) != 7 { return 5; }
            rd.v = 9;
        }
        None => { return 90; }
    }
    match o {
        Some(d) => { if d.v != 9 { return 50; } }
        None => { return 91; }
    }
    0
}
fn main() { std::process::exit(run()); }
