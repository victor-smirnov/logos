static G: i64 = 7i64;
struct H<'a> { p: *mut &'a i64 }
struct HM<'a> { m: &'a mut &'a i64 }
fn logos_main() -> i32 {
    let mut r: &'static i64 = &G;
    let q: *mut &'static i64 = &mut r;
    let h: H = H { p: q };
    let mut s: &'static i64 = &G;
    let hm: HM = HM { m: &mut s };
    return (unsafe { **h.p } + **hm.m - 14i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
