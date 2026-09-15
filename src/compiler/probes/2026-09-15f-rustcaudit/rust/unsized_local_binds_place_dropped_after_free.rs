unsafe extern "C" { fn malloc(n: usize) -> *mut u8; fn free(p: *mut u8); }
trait Tr { fn v(&self) -> i64; }
struct A { x: i64, c: *mut i64 }
impl Tr for A { fn v(&self) -> i64 { return self.x; } }
impl Drop for A { fn drop(self: &mut A) { unsafe { *self.c = *self.c + 1i64; } } }
struct Inner<T: ?Sized> { strong: i32, val: T }
struct MyRc<T: ?Sized> { inner: *mut Inner<T> }
impl<T: ?Sized> MyRc<T> {
    fn drop_me(&mut self) {
        unsafe {
            let _v: T = (*self.inner).val;
            free(self.inner as *mut u8);
        }
    }
}
fn main() {}
