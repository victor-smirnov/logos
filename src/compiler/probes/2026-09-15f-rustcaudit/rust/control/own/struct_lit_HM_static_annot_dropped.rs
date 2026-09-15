static G: i64 = 7i64;
struct HM<'a> { m: &'a mut &'a i64 }
fn logos_main() -> i32 {
    let mut s: &i64 = &G;
    let hm: HM = HM { m: &mut s };
    return (**hm.m - 7i64) as i32;
}
fn main() { std::process::exit(logos_main()); }
