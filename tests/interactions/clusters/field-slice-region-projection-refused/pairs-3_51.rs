trait Animal { fn grow(&mut self); }
struct Dog { age: i64 }
impl Animal for Dog { fn grow(&mut self) { self.age += 1; } }
fn main() {
    let mut d = Dog { age: 0 };
    let q: &mut dyn Animal = &mut d;
    let q2 = &mut *q;
    q2.grow();
    println!("{}", d.age);
}
