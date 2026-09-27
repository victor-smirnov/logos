enum S { D(i32), G(Vec<S>) }
fn main() {
    let mut vv: Vec<S> = Vec::new(); vv.push(S::D(1)); vv.push(S::D(2));
    let g = S::G(vv);
    let r = &g;
    match r { S::G(v) => { for x in v { match x { S::D(k) => println!("{}", k), _ => {} } } } _ => {} }
}
