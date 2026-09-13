# 2026-09-14a-ptrcoerce — BATCH 3 PREDICTIONS, BY NAME, before its build

Question: batch 2's aorecvsk roots an AddrOf receiver at ONE consumer of extract_borrow_place (the MethodCall arm).
dlog place_extract_consumers.dl (known answer `both ⊇ {visit}` held) lists 28 walker call sites in 8 contexts and 3
conflict askers (check_place_mut_use, visit ×2). Rooting Code::AddrOf INSIDE the walker is the delegation repair. Does
it close more, and what does it cost?

| probe        | predicted closed set                         | certainty |
|--------------|----------------------------------------------|-----------|
| ebpaddrof    | ⊇ {borrowck-loan-vec-content}                 | uncertain; a second row would be read, not assumed |
| ebpaddrofmut | ⊇ {borrowck-loan-vec-content}, ⊆ ebpaddrof    | uncertain |
Hand: J04 J06 J08 J09 close under both (as under aorecvsk). Legal B01 B03 B04 B06-B10 B11b B12 N01-N06: predicted
unmoved, and NAMED RISK: a shared `AddrOf` now rooted may make record_borrow deposit loans that nothing released before
(ebpaddrof only) — that is what the twin separates.
