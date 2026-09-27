



fn mk<F: FnOnce() -> Vec<i32>>(f: F) -> Vec<i32> { f() }
fn main() {
    let v = mk(Vec::new);
    println!("{}", v.len());
}
