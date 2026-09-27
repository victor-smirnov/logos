fn dup<T: Copy>(x: T) -> (T, T) { return (x, x); }
fn main() {
    let a: Option<i64> = Some(1);
    let p = a.clone();
    let (c, d) = dup(a);
    std::process::exit((p.unwrap() + c.unwrap() + d.unwrap()) as i32);
}
