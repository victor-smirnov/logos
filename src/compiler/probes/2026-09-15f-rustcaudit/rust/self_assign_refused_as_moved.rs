struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c + self.v; } } }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let mut r: i64 = 0i64;
    {
        let mut x: D = D { v: 1i64, c: p };
        x = x;
        r = x.v;
    }
    if r != 1i64 { return 2i32; }
    let got: i64 = unsafe { n };
    if got != 1i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
