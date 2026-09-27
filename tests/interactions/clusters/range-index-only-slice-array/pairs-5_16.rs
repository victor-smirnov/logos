use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn bump(xs: &mut [i64]) { for x in xs.iter_mut() { *x += 100; } }
fn main() {
    let mut a = [1i64, 2, 3, 4];
    bump(&mut a[1..3]);
    let s: &mut [i64] = &mut a[3..];
    s[0] = 0;
    println!("{:?}", a);
}
