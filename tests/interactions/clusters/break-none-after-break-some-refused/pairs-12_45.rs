fn f(v: &Vec<i64>) -> Option<i64> {
    let r = 'outer: loop {
        for x in v.iter() { if *x > 3 { break 'outer Some(*x); } }
        break None;
    };
    return r;
}
fn g(v: &Vec<i64>) -> Option<i64> {
    let r = loop { if v.len() > 9 { break Some(1); } break None; };
    return r;
}
fn main() { let v: Vec<i64> = vec![1, 5]; println!("{:?} {:?}", f(&v), g(&v)); }
