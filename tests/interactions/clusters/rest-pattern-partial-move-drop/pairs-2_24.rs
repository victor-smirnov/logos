struct Cfg { name: String, tag: Option<String> }
fn main() {
    let c = Cfg { name: String::from("base"), tag: None };
    let c2 = Cfg { tag: Some(String::from("t")), ..c };
    let Cfg { tag: Some(tg), .. } = c2 else { panic!("no") };
    println!("{}", tg);
}
