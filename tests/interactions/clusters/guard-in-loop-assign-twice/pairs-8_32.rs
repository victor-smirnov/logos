

fn main() {
    let mut acc = 0i64;
    for x in 0..5i64 {
        match x { n if n % 2 == 0 => { acc += n; } _ => { acc += 100; } }
    }
    println!("{}", acc);
}
