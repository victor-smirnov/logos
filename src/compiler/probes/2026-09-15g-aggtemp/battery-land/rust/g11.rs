struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
fn one<T>(x: T) -> [T; 1] { [x] }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    {
        let arr: [D; 1] = one(D { v: 1, c: p });
        assert_eq!(arr[0].v, 1);
    }
    let got: i64 = unsafe { n };
    println!("n={}", got);
}
