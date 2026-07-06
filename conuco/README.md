# Conuco

*Conuco* — a Taíno word for a small cultivation plot. Here it is the **incubator**:
an in-tree home for projects that **use** Logos and **co-evolve** with it.

## What lives here

Applications and libraries written in Logos, developed in lockstep with the
language itself — real workloads that exercise and put pressure on the compiler,
stdlib, Writ, and Deem while all of them are still moving.

## Why in-tree (not separate repos)

While a project and the language change together, a single tree removes the
friction at the seam: lockstep commits, no version pinning to juggle, one build,
one `git bisect`. A language change and the code that motivated it land in the
same commit.

## Lifecycle

```
seedling  →  grows in the conuco  →  graduates to its own repository
```

A project stays in `conuco/` only while it depends on unreleased language
changes. Once its footing is stable against a released toolchain slot, it is
transplanted out to a standalone repo and drops off this tree.

## Caveats

- These projects track the **in-tree compiler at HEAD** (pre-release) — expect churn.
- They are **not** part of the published toolchain / stdlib ABI surface.
- Each subproject carries its own `README.md` for build and run instructions.
