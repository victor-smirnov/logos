fn arr() -> [String; 3] { [String::from("a"), String::from("b"), String::from("c")] }
fn main() {
    let a: [String; 3] = arr();
    match a { [.., z] => { let _n = z.len(); } }
    match a { [.., w] => { let _m = w.len(); } }
}
