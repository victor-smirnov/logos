trait Buf { fn total(&self) -> i64; }
struct Sum { t: i64 }
impl Buf for Sum { fn total(&self) -> i64 { self.t } }
fn main() {
    let bb: Box<Box<dyn Buf>> = Box::new(Box::new(Sum { t: 5 }));
    println!("{}", bb.total());
}
