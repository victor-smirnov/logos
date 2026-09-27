
fn main() {
    let n: Box<i64> = Box::new(7);
    let o = move || n;
    let m = o();
    println!("{}", *m);
}
