fn main() {
    let mut a: [i64; 4] = [1, 2, 3, 4];
    let p: *mut i64 = &mut a[0] as *mut i64;
    let q: *mut i64 = &mut a[3] as *mut i64;
    let g: bool = q > p;
    let le: bool = p <= q;
    let ge: bool = p >= q;
    println!("g={} le={} ge={}", g as i32, le as i32, ge as i32);
    if !g { std::process::exit(1); }
    if !le { std::process::exit(2); }
    if ge { std::process::exit(3); }
}
