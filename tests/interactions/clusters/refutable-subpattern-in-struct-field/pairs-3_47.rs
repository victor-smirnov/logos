struct D { id: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
const PROTO: D = D { id: 7 };
static KEEP: D = D { id: 99 };
const BASE: i64 = 100;
struct Ring<const N: usize> { items: [D; N], head: usize }
impl<const N: usize> Ring<N> {
    fn total(&self) -> i64 { let mut t: i64 = 0; for d in self.items.iter() { t += d.id; } t }
    fn cap(&self) -> usize { N }
}
fn make3() -> Ring<3> { Ring::<3> { items: [D { id: BASE + 1 }, D { id: BASE + 2 }, D { id: BASE + 3 }], head: 0 } }
fn main() {
    let a = PROTO;
    println!("a {}", a.id);
    { let b = PROTO; println!("b {}", b.id + BASE); }
    println!("tmp {}", PROTO.id);
    println!("static {}", KEEP.id);
    let r = make3();
    println!("{} {} {}", r.total(), r.cap(), r.head);
    let Ring { items, head } = r;
    let [x, _, z] = items;
    println!("{} {} {}", x.id, z.id, head);
    println!("end");
}
