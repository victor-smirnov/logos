struct N { id: i32 }
fn keep_if<F: Fn(&N) -> bool>(v: Vec<N>, f: F) -> usize {
    let mut c = 0;
    for x in v { if f(&x) { c += 1; } }
    c
}
fn keep_g<T, F: Fn(&T) -> bool>(v: &Vec<T>, f: F) -> usize {
    let mut c = 0;
    for x in v.iter() { if f(x) { c += 1; } }
    c
}
fn main() {
    let mut v = Vec::new(); v.push(N { id: 1 }); v.push(N { id: 2 });
    println!("{}", keep_g(&v, |n: &N| n.id % 2 == 0));
    println!("{}", keep_g(&v, |n| n.id > 0));
    println!("{}", keep_if(v, |n| n.id % 2 == 0));
}
