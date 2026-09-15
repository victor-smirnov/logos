fn main() {
    let mut foo: Option<String> = Option::Some(String::from("foo"));
    let bar: &mut Option<String> = &mut foo;
    match *bar {
        Option::Some(ref mut baz) => {
            let moved: Option<String> = foo;
            let _n = baz.len();
        }
        Option::None => { }
    }
    std::process::exit(0);
}
