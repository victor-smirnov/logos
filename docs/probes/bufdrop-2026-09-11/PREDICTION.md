# PREDICTION — declared 2026-09-11, before the btree edit, after the buffered edit was measured

## THE CLASS, BY PROPERTY (not by the valgrind spelling)

Property: a stdlib struct whose **value-returning** constructor heap-allocates a
buffer it stores, which exposes an **explicit release function**, and which
declares **no `impl Drop`**.  Enumerated mechanically over all 9 `stdlib/**/*.logos`
struct declarations with a raw-pointer field and an allocating constructor:

    value-returning ctor  -> drop glue applies  -> CLASS
      1. BufWriter    stdlib/std/io/buffered/buffered.logos:51    close()
      2. BufReader    stdlib/std/io/buffered/buffered.logos:148   close()
      3. BTreeMap     stdlib/mem/collections/btree/btree.logos:27 btreemap_free()

    `*mut T`-returning ctor -> NO drop glue exists to hook -> NOT the class
      Chan, FutureSlot, Scheduler, SpscRing, ThreadPool  (5)
    passed-through non-owned pointer -> not an owner -> NOT the class
      BtssRemRet                                          (1)

    control side, same shape but WITH `impl Drop` (13): Arc Box BtvecBuf HashMap
      LineReader MemoryStore Mutex PdtHolder Rc RwLock String Vec VecDeque

The leak sweep's class was **2 members** because it was defined by which fixtures
valgrind caught.  The property's class is **3**, and the third is filed in
`unrowed_backlog.ledger` as a *single* with "no group yet".

## THE NUMBER

11 valgrind loss records over 8 fixtures go to 0; no exit code changes; no
`Invalid free` appears anywhere.

    MEASURED ALREADY (buffered.logos, candidate C, build-copy):
      bufio_generic_reader        1 rec     5 b   -> 0
      http_parse_stream           2 recs   96 b   -> 0
      http_framing_readers        3 recs   80 b   -> 0
      http_chunked_reader         1 rec    16 b   -> 0
      http_chunked_body_helper    1 rec    64 b   -> 0
      http_serialize_stream       1 rec    64 b   -> 0
                                  9 recs / 6 fixtures

    PREDICTED (btree.logos, not yet edited):
      deem_btreemap_source        2 recs  128 b   -> 0
      b1_btreemap_scope (hand)    2 recs  128 b   -> 0
                                  2 recs / 2 fixtures

    PREDICTED TO STAY CLEAN (the double-free direction):
      btreemap_basic          calls btreemap_free at line 82, then scope exit
      b2_btreemap_free_then_scope (hand)
      bufio_basic             already clean on base
      adv3_generic_field_method_call   already clean on base (exit 0, not 42)

## WHY BTreeMap NEEDS A SECOND EDIT THAT BufReader/BufWriter DID NOT

`BufWriter::close` nulls `self.buf` after `dealloc`; `btreemap_free` does **not**
null `keys`/`vals`.  A `Drop` added on top of the release function as it stands
double-frees at `btreemap_basic.logos:82`.  So the structural change is
"the release function nulls what it freed, and `Drop` calls the same guarded
release" — applied at both sites, which is what `close` already did at one.

## WHAT I EXPECT TO **DECLINE**, BY NAME

  * **element drops in BTreeMap.**  `btreemap_free` deallocs the two arrays and
    never drops the K/V elements; `remove` shifts elements and drops nothing
    either.  A `BTreeMap<String, String>` therefore leaks its elements on base
    and will still leak them after this change.  That is a DIFFERENT defect with
    different evidence, and dropping elements without also fixing `remove` is a
    double-drop.  Not bought here.
  * **the 5 `*mut T`-returning types.**  A raw pointer has no drop glue, so an
    `impl Drop` on them is inert.  Zero, and the zero is structural.

## WHAT WOULD REFUTE THE LANDING

An `Invalid free` on any fixture; any exit-code change; a leak record surviving;
a new refusal in `stdlib-cost` or `fail_text_oracle`.
