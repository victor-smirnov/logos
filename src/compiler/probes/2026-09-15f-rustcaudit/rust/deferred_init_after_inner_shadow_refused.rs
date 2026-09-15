struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(self: &mut D) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let x: D;
        {
            let x: D;
            x = D { v: 2i64, c: p };
            if x.v != 2i64 { return 9i32; }
        }
        x = D { v: 1i64, c: p };
        if x.v != 1i64 { return 8i32; }
    }
    if rd(p) != 21i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
