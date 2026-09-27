fn once(v: Vec<i32>) -> impl FnOnce() -> Vec<i32> { move || v }
fn main() { let f = once(vec![1]); f(); f(); }
