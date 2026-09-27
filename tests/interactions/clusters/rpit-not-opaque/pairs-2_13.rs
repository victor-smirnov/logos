trait Animal { fn legs(&self) -> i64; }
struct Dog { id: i64 }
impl Animal for Dog { fn legs(&self) -> i64 { 4 } }
fn get() -> impl Animal { Dog { id: 1 } }
fn main() { let a = get(); println!("{}", a.id); }
