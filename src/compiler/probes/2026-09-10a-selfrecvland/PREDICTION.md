# PREDICTION BY NAME, written on the UNARMED binary `911811129fca26d5 43`
# (baseline: tables/hand_base.txt), BEFORE the arm was applied.

REFUSE after the arm (was admitted):
  h1  by-value `self: S` vs stdlib `&mut Self`
  h2  SHARED-ref `self: &S` vs stdlib `&mut Self`   ← door 2 only
  h5  by-value on a GENERIC struct
  h7  NON-Drop trait `Tick`, `&mut Self` declared, by value written
  h9  non-Drop, trait `&mut Self`, impl `&S`        ← door 2 only
  h10 REVERSE: trait by value, impl `&mut S`
  h13 by-value Drop reached through a generic bound
  n9  trait `&Self`, impl `&mut S`                  ← door 2 only
  n10 trait `&mut Self`, impl `&S`                  ← door 2 only
  n12 stdlib `Drop` written by value

CHANGE SENTENCE, same rc (a text-only cost an rc column cannot see):
  h6  by-value Drop whose body moves a field out — was E0509, now the receiver

ADMIT, unchanged (the dangerous direction — a false refusal here condemns the arm):
  h3  CONTROL `&mut self` shorthand
  h4  CONTROL explicit `self: &mut S`
  h8  the 90-file shape: LOCAL by-value `trait Drop`
  n1  generic trait with its own type parameter
  n2  `Self` substituted to a GENERIC impl target, written in full
  n3  `&mut self` shorthand DECLARED, `self: &mut S` explicit in the impl
  n4  associated fn with NO receiver
  n6  by-value DECLARED and by-value written
  n7  trait method with a DEFAULT body, not overridden
  n8  LIFETIME-annotated declared receiver vs a bare impl receiver (rule 12)
  n11 stdlib `Drop` with the Rust-canonical `&mut Self` — and rc must stay 1
  n13 LOCAL `trait Drop` declaring by value — and rc must stay 1
  h11 h12 h_a h_b h_d h_e   already refused for another reason, unchanged
  h_c h_i                    already admitted, unchanged
