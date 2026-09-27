


fn fill(out: &mut Vec<i32>) { out.push(7); out.push(8); }
fn main() {
    let mut out = Vec::new();
    fill(&mut out);
    println!("{}", out.len());
}
