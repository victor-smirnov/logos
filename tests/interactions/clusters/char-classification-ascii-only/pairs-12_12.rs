fn main() {
    let cs = ['ö', 'Ж', 'é', 'a', '7', 'ß'];
    for c in cs.iter() { println!("{} {} {} {}", c.is_alphabetic(), c.is_alphanumeric(), c.is_uppercase(), c.is_lowercase()); }
}
