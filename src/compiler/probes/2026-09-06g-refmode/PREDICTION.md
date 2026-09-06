PREDICTED BY NAME BEFORE THE ARMED RUN (round 2026-09-06f, ref-mode block).
legend: P = compiles and runs exit 0;  R = refused;  ? = not predicted.

prog  shape                                     base   tupboth  atrefmode
h01   match (ref mut a, b), owned mut tuple      R      P        R
h02   match (ref a, b), read only                R      P        R
h03   ILLEGAL E0596: ref mut into a NON-mut local R     R(want)  R
h04   ILLEGAL: write through a shared `ref`       R      R        R
h05   ref mut passed to fn(&mut i64)              R      P        R
h06   nested ((ref mut a, b), c)                  R      P        R
h07   let (ref mut a, b) = t   [OTHER DOOR]       R      R        R
h08   if let (ref mut a, b) = t                   R      ?        R
h09   CONTROL variant payload ref mut             P      P        P
h10   CONTROL struct field ref mut                P      P        P
h11   E::N(ref mut n @ 1..=5)  [QUEUE ROW]        R      R        ?  (S2 unarmed)
h12   E::N(ref n @ 1..=5), read only              R      R        ?
h13   CONTROL plain at-binding n @ 1..=5          P      P        P
h14   CONTROL plain (a, b)                        P      P        P
h15   CONTROL (mut a, b) by value                 P      P        P
h16   CONTROL match &t { (a, b) }                 P      P        P
h17   2024-ILLEGAL match &t { (ref a, b) }        P      ?        P
h18   (ref s, b) over a MOVE-ONLY element         ?      P        ?
h19   two writes through one ref mut binding      R      P        R
h20   ref mut at index 1 of a 3-tuple             R      P        R
h21   ref mut on a STRUCT-typed element           ?      P        ?
h22   ref mut inside fn(mut t: (i64,i64))         R      P        R
h23   while let (ref mut a, b) = t                R      ?        R
h24   CONTROL or-shaped arms, plain bindings      P      P        P

DECLARED CRUDENESS (a probe is deliberately wrong): the tuple arm does NOT check
that the matched place is itself mutable, so h03 is expected to be ADMITTED under
`tupboth` — that is the probe's over-admission, not the shape of a fix.
