# Coding Tasks

Everything in [How Models Behave](models.md) is about models in isolation. When the task is *code*, those abstract properties acquire a specific, costly, and reproducible shape — and that shape forces a development methodology that Logos is built to materialize.

The argument here is deliberately grounded in the [information-theoretic view of model memory](models.md#the-information-theoretic-view-of-model-memory), not in engineering folklore. The folklore — "the model basically knows this language, it just needs a reminder," "a bigger model will close these gaps," "ask it to double-check" — is mostly wishful thinking, and the theory says *exactly* where it is right and where it is a trap. Logos's bet is that a platform designed from the structure of the problem beats one designed from the observation of its symptoms.

## The gap between local competence and global correctness

Models are extraordinarily good at **local-syntactic competence** — idiomatic, plausible, well-shaped code — and unreliable at **global-semantic correctness** — the artifact actually implementing the whole specification. For a large artifact (a compiler, a database, a standard library) the result is not a handful of missing features. It is a roughly **uniform distribution of shallow gaps**: every feature is represented, the common path of each works, and the tail of each is silently absent.

The uniformity is not random; it is the predicted output of optimizing for the *look* of completion. Finishing one feature exhaustively has diminishing plausibility-per-token (corner cases read as minor); starting the next feature has high marginal plausibility ("one more feature looks more complete"). A greedy generator therefore goes breadth-first and shallow. A whole-feature omission would *look* less complete than uniform-shallow coverage, so the model does not produce one. Put quantitatively: a specification is a conjunction of *N* behavioral constraints; an unconditioned generator satisfies each with some probability *p* < 1; the chance of satisfying all of them decays multiplicatively, so a roughly constant fraction is missed, smeared across constraints rather than concentrated. Uniform shallow gaps are what that arithmetic looks like in a codebase.

## The failure mode humans do not have

The omissions split into [OOD gaps](models.md#two-kinds-of-gap) (the mechanism was never learned) and [InD gaps](models.md#two-kinds-of-gap) (the mechanism is known but was not enumerated into the output). On code, the InD case dominates once the foundations exist, and it carries a property with no clean human analog: the gaps are **unknown-unknowns to the model itself**.

A competent engineer holds a model of their own ignorance — "I haven't handled the overflow case yet." The model cannot, because its assessment of completeness is generated from the same distribution as the code, with the same coverage bias; its "I implemented this fully" is another plausible completion, not a measurement. So the gaps are invisible not only to the model but to anyone who only *reads the model's output and trusts its confidence*. This is the failure mode naive process cannot catch — and it is why "have the model write its own tests" actively hides it: a test suite drawn from the same compressed model of the feature exercises exactly the subset that was implemented, passes, and tells you nothing.

## The methodology this forces

If the model cannot enumerate its own omissions, completeness cannot be established from inside the model. It must be driven from an **external, independent extent**. This is the load-bearing principle, and it is a theorem about the architecture, not a preference:

> Completeness must be checked from a distribution different from the one that produced the code — an independent test corpus, a reference implementation, a formal specification — whose support covers the regions the model's own model does not.

### The external corpus is mandatory, not a convenience

Importing a reference implementation's own test suite — for Logos, the `rustc`/`core`/`alloc`/`std` corpus — is not "good hygiene." It is the *only tractable* way to surface InD gaps, because the alternative (sampling the model to find them) is exponential on exactly the tail where the gaps live. The corpus **is** the materialized, scannable extent the model lacks: the index bolted onto a point-query engine. What is being offloaded is not a computation a generic solver provides (as with arithmetic) but *content* a reference's authors materialized — the externalized ledger of what must hold.

### Reading the discovery dynamics

Running an external corpus against an AI-built artifact produces a characteristic signal. Reading it correctly matters, because the obvious readings are wrong:

- **New gaps surface at a roughly constant rate per unit of corpus consumed.** This is the uniform-gap prediction confirmed in the field: the corpus keeps reaching fresh regions, and gap density there is about constant.
- **A constant rate means you are sampling, not draining.** The rate tracks coverage *growth*, not gap *depletion*. You cannot read "how close to done" off the rate alone — that needs a coverage measure over the requirement space, which is itself an external structural prior the rate does not contain.
- **Gap *complexity* falls even while the rate holds.** Deep, structural gaps (whole subsystems) are high-traffic and get hit early; the residual is the shallow within-feature tail. As the implemented extent grows, more new cases land in its *compressible interior*, so closing them is local. Late "gaps" increasingly look like ordinary **bugs** (mechanism present, an edge wrong) rather than gaps (mechanism absent). That is not loose naming — it is the OOD→InD frontier receding, a real ontological shift.
- **Beware the false summit.** When the rate finally drops, the dangerous reading is "we're done." The likely truth is "we exhausted the *corpus*, not the gaps." The residual lives in the corpus's *own* blind spots — what its authors thought to test — and resurfaces only in real use. A corpus's coverage measure is not the usage coverage measure, and the difference is exactly the set of gaps that ship.

### Two notions of completeness, and the generalizing pass

Passing the corpus is **instance coverage**. It does not establish **class generality**. Shown a failing test, the model makes the minimal local repair — the [generation/verification asymmetry](models.md#generation-is-not-verification) again: the fix is conditioned on the one instance, with no pressure to generalize to the whole problem-class, because generalizing would require enumerating the class, which the model cannot do. So a green corpus can be a stack of point-fixes that each satisfy one test without covering its class.

The diagnostic is whether new gaps increasingly recur in *already-touched* classes; if so, the fixes are pointwise. The remedy is a second, **generalizing pass**: for each passed test, externally identify its problem-class, generate sibling cases within the class (metamorphic variants, fuzzing around the construction), adjudicate them with the reference oracle, and measure per-class pass-rate. A class that passes only its imported instance is a point-fix; a high sibling-pass-rate is genuine generality. This converts instance coverage into class coverage, which is far more durable against real use. The honest bound: the generalizing pass itself rides an external class taxonomy — you generalize only within classes you *named*, and the residual moves up to "unnamed classes." The closed-world boundary rises one level rather than disappearing; but a level is a large gain.

### The model's role, correctly placed

This fixes the model's place in the loop. It is **not** the source of completeness (it cannot enumerate) and not merely a repairer. It is the **proposal distribution** that makes oracle-driven exploration tractable: it proposes good candidates — programs, sibling tests, fixes — cheaply, turning what would otherwise be blind exponential enumeration of the input space into a guided search. The external oracle adjudicates; verified results materialize into the growing extent; the larger extent expands the model's compressible interior, so the frontier of genuine novelty recedes. This is why the hybrid is more than "model plus lookup table": the model is the search heuristic, the symbolic corpus and oracle are the materialized, invertible extent, and neither alone closes the loop.

## How Logos materializes this

The methodology above is not a process document bolted onto an ordinary toolchain. The platform is shaped so that each step is cheap and first-class; the full treatment is in [New Requirements](requirements.md) and [Logos Fit](logos-fit.md), but the through-line is:

- **The external extent is a build-system citizen.** [lforge](../internals/lforge.md) treats an imported reference corpus as a managed input, not a pile of ad-hoc scripts: importing, trimming, and re-importing tests as gaps close is a workflow the build system understands.
- **The ledger is externalized and durable.** Gap catalogs — the list of known-missing behaviors keyed to the requirement space — are the materialized index the model cannot hold. They are project artifacts, not conversation history, and they are the operational form of "the list must come from outside."
- **Differential testing against a reference is a primitive.** Running a program through the reference and through Logos and diffing the behavior is meant to be cheap, because the reference oracle is the only thing that injects ground truth about the regions the model cannot self-report.
- **Hermes is the materialized, invertible substrate.** Where the model's knowledge is a forward-only compressed function, the platform's own data — diagnostics, the type registry, the gap ledger, test provenance — lives in a structured, navigable, *invertible* store. The platform supplies, as ordinary infrastructure, exactly the index the model cannot be.

The point of the chapter is the point of the project: Logos is built on the *theory* of why AI-authored code carries uniform unknown-unknown gaps — an information-theoretic property of how models store knowledge — rather than on the hope that a larger model or a cleverer prompt will make them go away. The methodology is what the theory requires; the platform is what makes the methodology cheap.
