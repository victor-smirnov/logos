fn main() {
    let arr = [10i32, 20, 30];
    let p = arr.as_ptr();
    let s: &[i32] = &arr;
    let q = s.as_ptr();
    unsafe { println!("{} {}", *p.add(1), *q.add(2)); }
}
