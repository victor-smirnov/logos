fn fill<T: Copy>(p: *mut T, n: usize, v: T) { for i in 0..n { unsafe { *p.add(i) = v; } } }
fn read_at<T: Copy>(p: *const T, i: usize) -> T { return unsafe { *p.add(i) }; }
fn main() {
    let mut buf = [0i64; 5];
    let p = &mut buf[0] as *mut _;
    fill(p, 3, 9);
    unsafe { *p.add(4) = -1; }
    println!("{:?}", buf);
    let x = read_at(&buf[0], 4);
    let y = read_at(&buf as *const i64, 1) * 2;
    println!("{} {}", x, y);
    let mut val = 10u8;
    let q = &mut val as *mut u8;
    unsafe { *q = (*q).wrapping_mul(30); }
    let r = q as *const u8;
    let copy = unsafe { *r };
    println!("{} {}", val, copy);
    let s = String::from("hey");
    let sp = &s as *const String;
    let len = unsafe { (&*sp).len() };
    println!("{}", len);
    let addr = p as usize;
    let back = addr as *mut i64;
    let v = unsafe { *back };
    println!("{} {}", v, std::ptr::eq(back, p));
    let mut pts = Vec::new();
    for i in 0..3 { pts.push(&buf[i] as *const i64); }
    let total: i64 = pts.iter().map(|pp| unsafe { **pp }).sum();
    println!("{}", total);
}
