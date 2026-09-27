fn main() {
    let a: u16 = 65530; let b: i64 = 5; let c: u8 = 250; let d: i32 = 7; let e: u64 = 9;
    println!("{:?}", b.checked_add(1));
    println!("{:?}", d.checked_add(1));
    println!("{:?}", e.checked_mul(2));
    println!("{:?}", c.checked_add(10));
    println!("{:?}", a.checked_add(10));
}
