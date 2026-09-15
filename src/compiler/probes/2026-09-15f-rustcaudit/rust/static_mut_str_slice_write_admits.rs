static mut NAME: &str = "init";
fn set() {
    let s = String::from("local");
    unsafe { NAME = s.as_str(); }
}
fn main() { set(); std::process::exit(0); }
