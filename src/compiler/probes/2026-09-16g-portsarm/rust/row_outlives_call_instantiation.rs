// twin of the CLOSED queue row outlives_call_instantiation (now a pass fixture)
fn outlives_dir<'a, 'b>(x: &'a i64, y: &'b i64) -> i32 where 'a: 'b { (*x + *y) as i32 }
fn foo<'p, 'q>(p: &'p i64, q: &'q i64) -> i32 { outlives_dir(p, q) }
fn bar(k: &i64, m: &i64) -> i32 { outlives_dir(k, m) }
fn main() {
    let a: i64 = 1; let b: i64 = 2;
    if foo(&a, &b) != 3 { std::process::exit(1); }
    if bar(&a, &b) != 3 { std::process::exit(2); }
    std::process::exit(0);
}
