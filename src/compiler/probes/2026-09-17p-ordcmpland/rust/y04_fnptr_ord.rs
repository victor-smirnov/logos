fn f1() -> i64 { 1 }
fn f2() -> i64 { 2 }
fn main() {
    let a: fn() -> i64 = f1; let b: fn() -> i64 = f2;
    let lt = a < b; let gt = a > b; let selfle = a <= a; let selflt = a < a;
    println!("lt={} gt={} selfle={} selflt={}", lt as i32, gt as i32, selfle as i32, selflt as i32);
    if lt == gt { std::process::exit(1); }
    if !selfle { std::process::exit(2); }
    if selflt { std::process::exit(3); }
}
