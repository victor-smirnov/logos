fn main() {
    let mut foo: Option<String> = Option::Some(String::from("foo"));
    match &mut foo {
        Option::Some(baz) => {
            let _t: Option<String> = foo.take();
            let _n = baz.len();
        }
        Option::None => { }
    }
    std::process::exit(0);
}
