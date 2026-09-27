struct Noisy { id: i32 }
impl Drop for Noisy { fn drop(&mut self) { println!("drop {}", self.id); } }
fn keep(v: Vec<Noisy>) -> Vec<Noisy> {
    let mut out = Vec::new();
    for x in v { if x.id % 2 == 0 { out.push(x); } }
    return out;
}
fn main() {
    let mut v = Vec::new(); for k in 10..14 { v.push(Noisy { id: k }); }
    let kept = keep(v);
    println!("kept {}", kept.len());
    let mut w = Vec::new(); for k in 20..24 { w.push(Noisy { id: k }); }
    let mut o2 = Vec::new();
    for x in w { if x.id % 2 == 1 { o2.push(x); } else { println!("skip {}", x.id); } }
    println!("o2 {}", o2.len());
}
