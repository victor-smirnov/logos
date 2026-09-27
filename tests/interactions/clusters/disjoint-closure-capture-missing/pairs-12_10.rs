struct R { id: i32 }
impl Drop for R { fn drop(&mut self) { println!("d{}", self.id); } }
fn make() -> impl Fn() -> i32 { let d = R { id: 7 }; return move || d.id; }
fn main() {
    let a = R { id: 1 };
    let b = R { id: 2 };
    let cl = move || a.id + b.id;
    println!("s{}", cl());
    let f = make();
    println!("f{}", f());
    drop(f);
    println!("end");
}
