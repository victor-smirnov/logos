struct PBox { p: *const i64 }
fn read(b: PBox) -> i64 { unsafe { *b.p } }
fn main() {
    let v: i64 = 6;
    let b = PBox { p: &v };
    std::process::exit((read(b) - 6) as i32);
}
