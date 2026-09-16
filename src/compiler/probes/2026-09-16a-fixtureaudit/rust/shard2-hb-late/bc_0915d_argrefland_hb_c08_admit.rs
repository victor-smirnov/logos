// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c08_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
struct H { inn: D }
fn run() -> i32 {
    let h = H { inn: D { v: 6 } };
    let rd: &D = &h.inn;
    if inner(&rd) != &h.inn as *const D { return 8; }
    let arr: [D; 2] = [D { v: 1 }, D { v: 2 }];
    let re: &D = &arr[1];
    if inner(&re) != &arr[1] as *const D { return 80; }
    0
}
fn main() { std::process::exit(run()); }
