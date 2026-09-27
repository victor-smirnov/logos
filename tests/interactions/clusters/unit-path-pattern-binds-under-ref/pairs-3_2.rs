enum E { A(i64), C }
fn h(e: &E) -> i64 { match e { E::C => 0, E::A(x) => *x } }
fn main() {
    let o: Option<String> = Some(String::from("hi"));
    let n = match &o { None => 0, Some(s) => s.len() as i64 };
    println!("{} {}", n, h(&E::A(7)));
    let v: Vec<Option<i64>> = vec![Some(1), None, Some(3)];
    let mut c: i64 = 0;
    for x in v.iter() { match x { None => { c += 100; } Some(k) => { c += *k; } } }
    println!("{}", c);
}
