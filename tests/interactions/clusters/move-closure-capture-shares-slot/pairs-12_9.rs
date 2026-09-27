#[derive(Clone, Copy)]
struct P { w: i64 }
fn main() {
    let mut x = 2i64;
    let s1 = move || x * 10;
    x = 7;
    println!("{} {}", s1(), x);
    let mut p = P { w: 2 };
    let s2 = move || p.w * 10;
    p.w = 7;
    println!("{} {}", s2(), p.w);
    let mut q = P { w: 3 };
    let s3 = move || q.w;
    q = P { w: 9 };
    println!("{} {}", s3(), q.w);
}
