



struct D { id: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
fn hs(t: String) -> String { t }
fn hv(t: Vec<i64>) -> Vec<i64> { t }
fn hd(t: D) -> D { t }
fn main() {
    let d = hd(D { id: 7 });
    println!("got {}", d.id);
    let v = hv(vec![1i64, 2, 3]);
    println!("v {}", v.len());
    let s = hs(String::from("yo"));
    println!("s {}", s);
}
