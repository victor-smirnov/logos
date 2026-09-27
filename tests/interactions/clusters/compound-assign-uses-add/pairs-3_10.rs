use std::ops::{Add, AddAssign};
#[derive(Clone, Copy)]
struct M { v: i64, p: i64 }
impl Add for M { type Output = M; fn add(self, o: M) -> M { M { v: self.v + o.v, p: 0 } } }
impl AddAssign for M { fn add_assign(&mut self, o: M) { self.v += o.v * 10; } }
fn accum<T: AddAssign + Copy>(dst: &mut T, x: T) { *dst += x; }
fn main() {
    let mut d = M { v: 1, p: 0 };
    d += M { v: 2, p: 0 };
    println!("{}", d.v);
    let mut a = [M { v: 1, p: 0 }];
    a[0] += M { v: 2, p: 0 };
    println!("{}", a[0].v);
    let mut e = M { v: 1, p: 0 };
    accum(&mut e, M { v: 2, p: 0 });
    println!("{}", e.v);
    let r = &mut e;
    *r += M { v: 1, p: 0 };
    println!("{}", e.v);
}
