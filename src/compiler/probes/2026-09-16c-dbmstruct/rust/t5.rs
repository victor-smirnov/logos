fn outlives_dir<'a,'b>(x: &'a i64, y: &'b i64) -> i32 where 'a: 'b { return (*x + *y) as i32; }
fn foo<'p,'q>(p: &'p i64, q: &'q i64) -> i32 { return outlives_dir(p, q); }
fn bar(k: &i64, m: &i64) -> i32 { return outlives_dir(k, m); }
fn main() { let a=1i64; let b=2i64; if foo(&a,&b)!=3 { std::process::exit(1); } if bar(&a,&b)!=3 { std::process::exit(2); } std::process::exit(0); }
