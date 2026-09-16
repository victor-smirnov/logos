#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut v: Vec<String> = Vec::new();
    v.push(String::from("a"));
    let e: &String = &v[0];
    v[0] = String::from("b");
    let _n: usize = e.len();
    return 0i32;
}
fn main() {}
