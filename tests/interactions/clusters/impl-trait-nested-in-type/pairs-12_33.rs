trait Counter { fn val(&self) -> i64; }
struct C { n: i64 }
impl Counter for C { fn val(&self) -> i64 { return self.n; } }
fn read_boxed(c: Box<impl Counter>) -> i64 { return c.val() + 1; }
fn main() {
    let b = Box::new(C { n: 6 });
    let x = read_boxed(b);
    let y = read_boxed(Box::new(C { n: 5 }));
    println!("{} {}", x, y);
}
