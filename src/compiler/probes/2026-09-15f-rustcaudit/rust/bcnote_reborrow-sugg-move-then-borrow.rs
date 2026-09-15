struct State { v: i64 }
fn consume(s: &mut State) {}
fn fill(state: &mut State) {
    let moved: &mut State = state;
    consume(moved);
    fill_segment(state);
}
fn fill_segment(state: &mut State) {}
fn main() {}
