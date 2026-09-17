struct P { x: i64, y: i64 }
fn get(o: Option<P>) -> i64 {
    let Some(P { x, y }) = o else { return 77; };
    return x + y;
}
fn main() {
    let o = Some(P { x: 3, y: 4 });
    if get(o) != 7 { std::process::exit(1); }
}
