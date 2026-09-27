struct Words<'a> { s: &'a str, done: bool }
impl<'a> Iterator for Words<'a> {
    type Item = &'a str;
    fn next(&mut self) -> Option<&'a str> {
        if self.done { return None; }
        self.done = true;
        Some(self.s)
    }
}
fn main() {
    let w = Words { s: "abc", done: false };
    let v = w.map(|x| x.len() as i64).collect::<Vec<i64>>();
    println!("{:?}", v);
}
