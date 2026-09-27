use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
trait HasZero { const ZERO: Self; }
impl HasZero for i64 { const ZERO: i64 = 0; }
fn z<T: HasZero>() -> T { return T::ZERO; }
fn main() { let a: i64 = z(); println!("{} {}", a, i64::ZERO); }
