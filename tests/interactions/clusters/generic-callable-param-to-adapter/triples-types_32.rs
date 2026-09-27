fn main() {
    let k = 3i64;
    let f = move |x: i64| x * k;
    let v: Vec<i64> = (1i64..4).map(&f).collect();
    let w: Vec<i64> = v.iter().map(|x| f(*x)).collect();
    println!("{:?} {:?} {}", v, w, f(10));
}
