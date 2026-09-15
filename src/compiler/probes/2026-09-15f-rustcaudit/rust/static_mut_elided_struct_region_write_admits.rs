static V: i64 = 5i64;
struct H<'a> { r: &'a i64 }
static mut HS: H = H { r: &V };
fn set() {
    let n: i64 = 1i64;
    unsafe {
        HS = H { r: &n };
        HS.r = &n;
    }
}
fn main() { set(); unsafe { std::process::exit(*HS.r as i32); } }
