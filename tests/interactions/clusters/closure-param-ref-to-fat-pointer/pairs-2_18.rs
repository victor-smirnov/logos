trait Op { fn apply(&self, x: i64) -> i64; fn name(&self) -> String; }
struct AddN { n: i64 }
impl Op for AddN { fn apply(&self, x: i64) -> i64 { x + self.n } fn name(&self) -> String { format!("+{}", self.n) } }
fn main() {
    let mut ops: Vec<Box<dyn Op>> = Vec::new();
    ops.push(Box::new(AddN { n: 3 })); ops.push(Box::new(AddN { n: 4 }));
    let names: Vec<String> = ops.iter().map(|o| o.name()).collect();
    for n in names.iter() { print!("{},", n); } println!("");
}
