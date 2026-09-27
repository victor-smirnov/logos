#[derive(Clone,Copy)] struct P { w: i64 }
fn main() {
    let mut p = P { w: 2 };
    let s = move || p.w;
    p.w = 7;
    println!("{}", s());
}
