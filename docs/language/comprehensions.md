# List and Map Comprehensions

Logos has comprehension syntax for building collections from an iterable. There are two flavours: **plain** comprehensions that produce standard-library containers (`Vec<T>`, `HashMap<K, V>`), and **Writ** comprehensions (prefixed with `@`) that produce Writ documents directly.

The shape mirrors Python's:

```
[ expr        for x in iter ( if guard )? ]      // list → Vec<T>
{ key : val   for x in iter ( if guard )? }      // map  → HashMap<K, V>
@[ expr       for x in iter ( if guard )? ]      // Writ list (ObjectArray)
@{ key : val  for x in iter ( if guard )? }      // Writ map
```

The optional `if guard` filters elements before they reach the output.

## List Comprehensions → `Vec<T>`

```logos
use logos.mem.collections.vec;

let arr: [i32; 5] = [1, 2, 3, 4, 5];

let doubled: Vec<i32>  = [x * 2  for x in arr];                  // [2, 4, 6, 8, 10]
let even_sq: Vec<i32>  = [x * x  for x in arr if (x % 2) == 0];  // [4, 16]
```

The output type is inferred from the body expression. Guard expressions are arbitrary Logos booleans.

## Map Comprehensions → `HashMap<K, V>`

```logos
use logos.mem.collections.hashmap;

let arr: [i32; 5] = [1, 2, 3, 4, 5];

let sq: HashMap<i32, i32> = {x: x * x  for x in arr};
let ev: HashMap<i32, i32> = {x: x * 10 for x in arr if (x % 2) == 0};
```

The key/value expressions can use any binding introduced by the `for` clause; both are evaluated for each kept element.

`HashMap<K, V>` is part of the standard library at `stdlib/mem/collections/hashmap` (`logos.mem.collections.hashmap`), with support for primitive keys (integers, `bool`) and the `Hash`/`Eq` infrastructure required for user keys. See `std_hashmap_basic`, `std_hashmap_full`, `std_hashmap_iter`, `std_hashmap_multi_k` in the test suite.

## Writ List Comprehensions → `Rc<Writ>` (`ObjectArray` root)

```logos
use logos.lang.writ.comp_builder;
use logos.lang.writ.container;
use logos.lang.writ.anyval;
use logos.lang.rc;

let arr: [i32; 5] = [1, 2, 3, 4, 5];

let doc: Rc<Writ> =
    @[ x * x for x in arr ];

let evens: Rc<Writ> =
    @[ x * 10 for x in arr if (x % 2) == 0 ];
```

The result is a fresh `Rc<Writ>` document whose root is an `ObjectArray`. Element expressions are plain scalars that coerce into the document (no `AnyVal` wrapping). The Writ form gives you a relocatable, schema-tagged container in one expression.

## Writ Map Comprehensions → `Rc<Writ>` (Map root)

```logos
use logos.lang.writ.comp_builder;
use logos.lang.writ.container;
use logos.lang.writ.anyval;
use logos.lang.rc;

let keys: [str; 4] = ["a", "b", "c", "d"];
let idx:  [i32; 4] = [0, 1, 2, 3];

let doc: Rc<Writ> =
    @{ keys[x as i64] : x * 100 for x in idx };
```

The key expression must be a `str`; values are plain scalars that coerce into the document.

## Notes

- Comprehensions use the same iteration protocol as `for` loops; anything iterable in a `for` is iterable in a comprehension.
- The body and guard see all bindings from the surrounding scope, the same way a closure body would.
- Multi-clause / nested-`for` comprehensions (Python's `[x for xs in mat for x in xs]`, multiple `for`/`if` in sequence) are **planned** but not yet implemented; for now, chain through helpers or a temporary collection. See [Roadmap](../roadmap.md).

## Tests

- `list_comp_basic`, `map_comp_basic` — plain forms.
- `writ_list_comp_basic`, `writ_list_comp_edge`, `writ_map_comp_basic`, `writ_map_comp_multi` — Writ forms.
