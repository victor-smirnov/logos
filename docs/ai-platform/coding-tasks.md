# Coding Tasks

When the task is *code*, the abstract properties in [How Models Behave](models.md) acquire a specific, costly, reproducible shape that forces a development methodology Logos is built to materialize. The argument is grounded in the [information-theoretic view of model memory](models.md#the-information-theoretic-view-of-model-memory), not folklore. The folklore — "the model knows this language, it just needs a reminder," "a bigger model closes these gaps," "ask it to double-check" — is mostly wishful; the theory says exactly where it is right and where it is a trap. Logos's bet: a platform designed from the problem's structure beats one designed from its symptoms.

## The gap between local competence and global correctness

Models excel at **local-syntactic competence** (idiomatic, plausible code) and are unreliable at **global-semantic correctness** (the artifact implementing the whole specification). For a large artifact (compiler, database, stdlib) the result is not a few missing features but a roughly **uniform distribution of shallow gaps**: every feature present, each common path working, each tail silently absent.

The uniformity is the predicted output of optimizing for the *look* of completion. Finishing one feature exhaustively has diminishing plausibility-per-token (corner cases read as minor); starting the next has high marginal plausibility. A greedy generator goes breadth-first and shallow; a whole-feature omission would look less complete than uniform-shallow coverage, so it is not produced. Quantitatively: a spec is a conjunction of *N* constraints, an unconditioned generator satisfies each with probability *p* < 1, the joint probability decays multiplicatively, so a roughly constant fraction is missed and smeared across constraints rather than concentrated.

## The failure mode humans do not have

Omissions split into [OOD gaps](models.md#two-kinds-of-gap) (mechanism never learned) and [InD gaps](models.md#two-kinds-of-gap) (mechanism known, not enumerated into output). On code, InD dominates once foundations exist, and it carries a property with no clean human analog: the gaps are **unknown-unknowns to the model itself**.

A competent engineer models their own ignorance ("haven't handled overflow yet"). The model cannot: its completeness assessment is generated from the same distribution as the code, with the same coverage bias, so "I implemented this fully" is another plausible completion, not a measurement. The gaps are invisible to the model and to anyone who only reads its output and trusts its confidence. This is what naive process cannot catch — and why "have the model write its own tests" actively hides it: a suite drawn from the same compressed feature-model exercises exactly the implemented subset, passes, and tells you nothing.

## The methodology this forces

If the model cannot enumerate its own omissions, completeness cannot come from inside the model. It must be driven from an **external, independent extent** — a theorem about the architecture, not a preference:

> Completeness must be checked from a distribution different from the one that produced the code — an independent test corpus, a reference implementation, a formal specification — whose support covers the regions the model's own model does not.

### The external corpus is mandatory, not a convenience

Importing a reference's test suite (for Logos, the `rustc`/`core`/`alloc`/`std` corpus) is the *only tractable* way to surface InD gaps: the alternative (sampling the model) is exponential on exactly the tail where gaps live. The corpus **is** the materialized, scannable extent the model lacks — the index bolted onto a point-query engine. What is offloaded is not a computation a generic solver provides (as with arithmetic) but *content* a reference's authors materialized: the externalized ledger of what must hold.

### Reading the discovery dynamics

Running an external corpus against an AI-built artifact produces a characteristic signal; the obvious readings are wrong:

- **New gaps surface at a roughly constant rate per unit of corpus consumed.** The uniform-gap prediction confirmed: the corpus reaches fresh regions, gap density there is ~constant.
- **A constant rate means sampling, not draining.** The rate tracks coverage *growth*, not gap *depletion*. "How close to done" needs a coverage measure over the requirement space — an external structural prior the rate does not contain.
- **Gap *complexity* falls while the rate holds.** Deep structural gaps (whole subsystems) are high-traffic and hit early; the residual is the shallow within-feature tail. As the implemented extent grows, new cases land in its *compressible interior*, so closing them is local. Late "gaps" increasingly look like ordinary **bugs** (mechanism present, edge wrong) — the OOD→InD frontier receding, a real ontological shift.
- **Beware the false summit.** When the rate drops, the dangerous reading is "done." The likely truth is "exhausted the *corpus*, not the gaps." The residual lives in the corpus's own blind spots (what its authors thought to test) and resurfaces in real use; corpus coverage ≠ usage coverage, and the difference is the set of gaps that ship.

### Two notions of completeness, and the generalizing pass

Passing the corpus is **instance coverage**, not **class generality**. Shown a failing test, the model makes the minimal local repair — the [generation/verification asymmetry](models.md#generation-is-not-verification) again: the fix is conditioned on the one instance with no pressure to generalize to the class (generalizing requires enumerating the class). So a green corpus can be a stack of point-fixes each satisfying one test without covering its class.

Diagnostic: whether new gaps increasingly recur in *already-touched* classes; if so, the fixes are pointwise. Remedy — a second, **generalizing pass**: for each passed test, externally identify its problem-class, generate sibling cases (metamorphic variants, fuzzing around the construction), adjudicate with the reference oracle, measure per-class pass-rate. A class passing only its imported instance is a point-fix; high sibling-pass-rate is genuine generality. This converts instance coverage into class coverage, far more durable against real use. Honest bound: the generalizing pass rides an external class taxonomy — you generalize only within classes you *named*, and the residual moves to "unnamed classes." The closed-world boundary rises one level rather than disappearing — but a level is a large gain.

### The model's role, correctly placed

The model is **not** the source of completeness (it cannot enumerate) and not merely a repairer. It is the **proposal distribution** that makes oracle-driven exploration tractable: it proposes good candidates (programs, sibling tests, fixes) cheaply, turning blind exponential enumeration into guided search. The external oracle adjudicates; verified results materialize into the growing extent; the larger extent expands the model's compressible interior, so the novelty frontier recedes. The hybrid is more than "model plus lookup table": the model is the search heuristic, the symbolic corpus and oracle are the materialized invertible extent, and neither alone closes the loop.

## How Logos materializes this

Each step is cheap and first-class (full treatment in [New Requirements](requirements.md) and [Logos Fit](logos-fit.md)):

- **The external extent is a build-system citizen.** [lforge](../internals/lforge.md) treats an imported reference corpus as a managed input: importing, trimming, and re-importing tests as gaps close is a workflow the build system understands.
- **The ledger is externalized and durable.** Gap catalogs — known-missing behaviors keyed to the requirement space — are the materialized index the model cannot hold. They are project artifacts, not conversation history: the operational form of "the list must come from outside."
- **Differential testing against a reference is a primitive.** Running a program through the reference and through Logos and diffing is cheap, because the reference oracle is the only injector of ground truth about regions the model cannot self-report.
- **Hermes is the materialized, invertible substrate.** Where the model's knowledge is a forward-only compressed function, the platform's own data — diagnostics, type registry, gap ledger, test provenance — lives in a structured, navigable, *invertible* store. The platform supplies, as ordinary infrastructure, exactly the index the model cannot be.

Logos is built on the *theory* of why AI-authored code carries uniform unknown-unknown gaps — an information-theoretic property of how models store knowledge — not on the hope that a larger model or cleverer prompt removes them. The methodology is what the theory requires; the platform is what makes it cheap.
