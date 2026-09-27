struct D { id: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
fn hd(t: D) -> D { t }
fn hs(t: String) -> String { t }
fn main() {
    let d = hd(D { id: 7 });
    println!("got {}", d.id);
    let s = hs(String::from("yo"));
    println!("s {}", s);
}
