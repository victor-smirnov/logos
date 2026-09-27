fn main() {
    let f = Vec::<i64>::new;
    let mut v = f();
    v.push(1);
    let g: fn() -> String = String::new;
    println!("{} {}", v.len(), g().len());
}
