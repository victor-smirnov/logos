struct Noisy { id: i64 }
impl Drop for Noisy { fn drop(&mut self) { println!("drop {}", self.id); } }
trait Factory { type Out; fn make(&self) -> Self::Out; }
struct NF { k: i64 }
impl Factory for NF { type Out = Noisy; fn make(&self) -> Noisy { Noisy { id: self.k } } }
fn produce<F: Factory>(f: &F) { let o = f.make(); println!("made"); }
fn produce_t<T>(t: T) { println!("got"); }
fn main() {
    produce(&NF { k: 1 });
    produce_t(Noisy { id: 2 });
    println!("end");
}
