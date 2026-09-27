use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
enum Tok { Op(char), End }
fn f(t: &Tok) -> i64 {
    return match t { Tok::Op(c @ ('+' | '-')) => { if *c == '+' { 1 } else { 2 } } Tok::Op(_) => 3, Tok::End => 0 };
}
fn main() { println!("{} {} {}", f(&Tok::Op('+')), f(&Tok::Op('-')), f(&Tok::Op('/'))); }
