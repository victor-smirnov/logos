struct D(i32);
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.0); } }
fn f() -> i32 { let _p = D(1); D(2).0 }
fn main() {
    let blk = { let _p = D(3); D(4).0 + 1 };
    println!("blk {}", blk);
    println!("f {}", f());
}
