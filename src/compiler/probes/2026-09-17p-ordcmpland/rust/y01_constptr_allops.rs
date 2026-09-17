fn main() {
    let a: [u8; 4] = [1, 2, 3, 4];
    let p: *const u8 = &a[0] as *const u8;
    let q: *const u8 = &a[3] as *const u8;
    let lt = p < q; let gt = p > q; let le = p <= q; let ge = p >= q;
    let selfle = p <= p; let selflt = p < p;
    println!("lt={} gt={} le={} ge={} selfle={} selflt={}",
        lt as i32, gt as i32, le as i32, ge as i32, selfle as i32, selflt as i32);
    if !lt { std::process::exit(1); }
    if gt { std::process::exit(2); }
    if !le { std::process::exit(3); }
    if ge { std::process::exit(4); }
    if !selfle { std::process::exit(5); }
    if selflt { std::process::exit(6); }
}
