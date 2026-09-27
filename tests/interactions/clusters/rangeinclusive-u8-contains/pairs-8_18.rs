fn main() {
    let r = 97u8..=122u8;
    let a: u8 = 101; let b: u8 = 100; let c: u8 = 65;
    println!("{} {} {}", r.contains(&a), r.contains(&b), r.contains(&c));
    println!("{} {}", (1i64..=10).contains(&5), (1i32..=10).contains(&5));
}
