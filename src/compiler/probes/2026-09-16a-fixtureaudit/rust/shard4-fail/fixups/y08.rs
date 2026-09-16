#![allow(dead_code)]
// TWIN v2: Logos elides the returned `&mut`'s lifetime; Rust needs it spelled
// (v1 died on E0106 before reaching the defect). Tied to the receiver.
fn f<'a, 'v>(v: &'v mut Vec<&'static i64>) -> &'v mut [&'a i64] {
    return v;
}
fn main() {}
