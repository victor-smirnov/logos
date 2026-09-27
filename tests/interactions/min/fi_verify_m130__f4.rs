struct G { d: i64 }
impl Drop for G { fn drop(&mut self) { println!("drop {}", self.d); } }
fn mk(d: i64) -> G { G { d } }
fn f() -> i64 { let _a = mk(1); mk(2).d }
fn main() { let r = f(); println!("f {}", r); }
