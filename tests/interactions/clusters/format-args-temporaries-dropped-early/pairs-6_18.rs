struct D { id: i32 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
fn len_of(d: &D) -> i32 { d.id * 2 }
fn show(a: i32, b: i32) { println!("show {} {}", a, b); }
fn main() {
    show(len_of(&D { id: 1 }), 5);
    println!("t {}", len_of(&D { id: 2 }));
}
