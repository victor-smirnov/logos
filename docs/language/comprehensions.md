# List and Map Comprehensions

Logos has comprehension syntax for building collections from an iterable. There are two flavours: **plain** comprehensions that produce standard-library containers (`Vec<T>`, `HashMap<K, V>`), and **Hermes** comprehensions (prefixed with `@`) that produce Hermes documents directly.

The shape mirrors Python's:

```
[ expr        for x in iter ( if guard )? ]      // list → Vec<T>
{ key : val   for x in iter ( if guard )? }      // map  → HashMap<K, V>
@[ expr       for x in iter ( if guard )? ]      // Hermes list (ObjectArray)
@{ key : val  for x in iter ( if guard )? }      // Hermes map
```

The optional `if guard` filters elements before they reach the output.

## List Comprehensions → `Vec<T>`

```logos
use std.collections.vec;

let arr: [i32; 5] = [1, 2, 3, 4, 5];

let doubled: Vec<i32>  = [x * 2  for x in arr];                  // [2, 4, 6, 8, 10]
let even_sq: Vec<i32>  = [x * x  for x in arr if (x % 2) == 0];  // [4, 16]
```

The output type is inferred from the body expression. Guard expressions are arbitrary Logos booleans.

## Map Comprehensions → `HashMap<K, V>`

```logos
use std.collections.hashmap;

let arr: [i32; 5] = [1, 2, 3, 4, 5];

let sq: HashMap<i32, i32> = {x: x * x  for x in arr};
let ev: HashMap<i32, i32> = {x: x * 10 for x in arr if (x % 2) == 0};
```

The key/value expressions can use any binding introduced by the `for` clause; both are evaluated for each kept element.

`HashMap<K, V>` is part of the standard library at `stdlib/std/collections/hashmap`, with support for primitive keys (integers, `bool`) and the `Hash`/`Eq` infrastructure required for user keys. See `std_hashmap_basic`, `std_hashmap_full`, `std_hashmap_iter`, `std_hashmap_multi_k` in the test suite.

## Hermes List Comprehensions → `Hermes` (`ObjectArray` root)

```logos
use logos.mem.hermes.ctr;
use std.hermes.anyval;

let arr: [i32; 5] = [1, 2, 3, 4, 5];

let doc: Hermes =
    @[ AnyVal::embed_i24(x * x) for x in arr ];

let evens: Hermes =
    @[ AnyVal::embed_i24(x * 10) for x in arr if (x % 2) == 0 ];
```

The result is a fresh `Hermes` document whose root is an `ObjectArray` of `AnyVal`. Element expressions must produce `AnyVal` (use the `embed_*` constructors or wrap user types). The Hermes form gives you a relocatable, schema-tagged container in one expression.

## Hermes Map Comprehensions → `Hermes` (Map root)

```logos
let keys: [str; 10] = ["k0","k1","k2","k3","k4","k5","k6","k7","k8","k9"];
let idx:  [i32; 10] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9];

let doc: Hermes =
    @{ keys[x as i64] : AnyVal::embed_i24(x * 100) for x in idx };
```

Keys may be any expression typeable into the Hermes key surface (currently strings); values are `AnyVal`.

## Notes

- Comprehensions use the same iteration protocol as `for` loops; anything iterable in a `for` is iterable in a comprehension.
- The body and guard see all bindings from the surrounding scope, the same way a closure body would.
- Multi-clause / nested-`for` comprehensions (Python's `[x for xs in mat for x in xs]`, multiple `for`/`if` in sequence) are **planned** but not yet implemented; for now, chain through helpers or a temporary collection. See [Roadmap](../roadmap.md).

## Tests

- `list_comp_basic`, `map_comp_basic` — plain forms.
- `hermes_list_comp_basic`, `hermes_list_comp_edge`, `hermes_map_comp_basic`, `hermes_map_comp_multi` — Hermes forms.
