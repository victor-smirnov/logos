fn f<B>(p: Option<B>) -> i64 { 1 }
fn h(p: Option<i64>) -> i64 { 2 }
fn main() {
    let y = f::<i64>(Some(7));
    println!("{}", y);
}
