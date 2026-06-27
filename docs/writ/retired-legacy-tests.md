# legacy test adaptation log (step 6 of the writ cut-over)

UPDATE 2026-06-11: per Victor's direction, NO tests were retired — all 78
legacy-era tests were ADAPTED to writ equivalents preserving their
coverage intent. The adaptation built these writ surfaces:

- mem.writ.equal / hashing / clone / check — direct HAny walkers (the
  stringify foundation pattern; the legacy tag_dispatch tables stay retired).
- mem.writ.parser — extended with bare-ident keys, typed dense containers
  (<T>[...] / <K[,AnyVal]>{...} incl strictness rules), @Type(params)?=init
  prefix + "init"@Type(params) postfix typed values with @Decimal(P,S)
  validation; stringify renders the canonical postfix form.
- lang.writ.decimal — hdec_from_str / hdec_cmp / hdec_add on the
  u64-coefficient model (the limb bignum is gone; overflow is an error).
- lang.writ.typed_arr — MapSlice* + writ2_build_map_*_anyval revive the
  `as <K,AnyVal>{}` casts on value-form &[HAny] (Refs re-anchor via clone).
- lang.fabric.string_store — the SoA string storage (Buffer<StrDt>); bare
  scalars ride the Primitive blanket (Buffer<i64>).
- mem.writ.tag_system — w_tag_size; registry lookups verified live.

Semantic shifts (writ model differences, intentional):
- parse errors -> null handles (the lenient parser has no Result channel);
- clone IS compactify (live-set copy);
- holder model: Zone/DataRef/DataOwn -> Rc<Writ> + Held<T> + RAII;
- HBS wire = the mem.writ.hbs codec (tag-by-tag), import = clone_into.
