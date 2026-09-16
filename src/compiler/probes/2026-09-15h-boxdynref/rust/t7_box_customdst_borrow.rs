struct S { n: i64, d: [i64] }
fn use_s(s: &S) -> i64 { s.n + s.d.len() as i64 }
fn main() {
    let b: Box<S> = {
        let v: Box<[i64]> = vec![1i64, 2, 3].into_boxed_slice();
        unsafe { std::mem::transmute::<(*mut i64, usize), Box<S>>((Box::into_raw(v) as *mut i64, 3)) }
    };
    let _ = use_s(&b);
}
