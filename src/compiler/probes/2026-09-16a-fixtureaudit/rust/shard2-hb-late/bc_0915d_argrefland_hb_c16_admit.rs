// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_c16_admit.logos
struct D { v: i64 }
#[allow(dead_code)] fn inner(a: &&D) -> *const D { let x: &D = *a; x as *const D }
fn run() -> i32 {
    let arr: [D; 3] = [D { v: 1 }, D { v: 2 }, D { v: 3 }];
    let mut i: usize = 0;
    while i < 3 {
        let rd: &D = &arr[i];
        if inner(&rd) != &arr[i] as *const D { return 16; }
        i = i + 1;
    }
    0
}
fn main() { std::process::exit(run()); }
