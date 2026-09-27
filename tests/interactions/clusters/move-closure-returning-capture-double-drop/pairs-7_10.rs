struct Noisy { id: i64 }
impl Drop for Noisy { fn drop(&mut self) { println!("drop {}", self.id); } }
fn main() {
    let n = Noisy { id: 2 };
    let o = move || n;
    let m = o();
    println!("got {}", m.id);
}
