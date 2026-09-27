fn main() {
    let words = "the quick fox";
    let v = words.split(' ').map(|w| w.len() as i64).collect::<Vec<i64>>();
    println!("{:?}", v);
}
