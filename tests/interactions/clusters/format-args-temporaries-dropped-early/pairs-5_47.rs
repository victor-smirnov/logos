use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
use std::ops::{Add, Sub, Mul, Neg, Index, IndexMut, AddAssign};
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, o: &D) -> bool { return self.v == o.v; } }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.v); } }
fn show(b: bool, n: i64) { println!("show {} {}", b, n); }
fn main() {
    let x = D { v: 1 };
    println!("{}", x == D { v: 1 });
    show(x == D { v: 2 }, 0);
    println!("end");
}
