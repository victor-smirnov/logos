struct S { p: *mut i32 }
struct G<T> { p: *mut T }
fn mk<T>() -> G<T> { return G { p: std::ptr::null_mut() }; }
fn main() {
    let a: *mut i32 = std::ptr::null_mut();
    let s = S { p: std::ptr::null_mut() };
    let g: G<i64> = mk();
    println!("{} {} {}", a.is_null(), s.p.is_null(), g.p.is_null());
}
