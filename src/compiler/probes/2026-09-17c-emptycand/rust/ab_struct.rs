static V: i64 = 5;
struct W<'s> { r: &'s i64 }
fn pick<'a>(x: &W<'a>, y: &W<'a>) -> &'a i64 { if *x.r > *y.r { x.r } else { y.r } }
fn escape() -> &'static i64 { let n: i64 = 9; let a = W{r:&V}; let b = W{r:&n}; pick(&a,&b) }
fn main() { println!("{}", escape()); }
