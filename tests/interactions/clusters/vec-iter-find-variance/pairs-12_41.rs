fn fe(v: &Vec<i64>) -> i64 { let mut it = v.iter(); let f = it.find(|x| **x % 2 == 0); return match f { Some(x) => *x, None => -1 }; }
fn main() {
    let v: Vec<i64> = vec![1, 4];
    let mut it = v.iter();
    let g = it.find(|x| **x > 1);
    println!("{:?} {}", g.copied(), fe(&v));
}
