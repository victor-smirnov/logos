#[derive(Clone, Debug, PartialEq)]
struct Tok { id: i32, label: String }
impl Drop for Tok { fn drop(&mut self) { println!("drop {} {}", self.id, self.label); } }
#[derive(Clone, Debug)]
struct Pair { a: Tok, b: Tok }
fn main() {
    let t = Tok { id: 1, label: String::from("one") };
    let c = t.clone();
    println!("eq {} {:?}", t == c, c);
    {
        let p = Pair { a: t, b: Tok { id: 2, label: String::from("two") } };
        let q = p.clone();
        println!("q {} {}", q.a.id, q.b.label);
    }
    let mut v: Vec<Tok> = Vec::new();
    for i in 0..3 { v.push(Tok { id: 10 + i, label: format!("v{}", i) }); }
    let w = v.clone();
    v.truncate(1);
    println!("lens {} {}", v.len(), w.len());
    let o = Some(c.clone());
    if let Some(ref x) = o { println!("some {:?}", x); }
    println!("end");
}
