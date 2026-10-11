# Notes: 1.1 literature (search of 2026-10-10)

Tags: V = abstract or full text opened and confirmed; U = from search summary only, or the venue is from memory. Re-verify U items before citing.

## Low-resource and new languages

- Cassano et al., MultiPL-E, arXiv 2208.08227 (2022; TSE 2023 U). Performance correlates with language popularity, but some niche languages (Lua) do as well as popular ones. Static typing "neither helps nor hinders" single-shot generation (p = 0.33, 0.23). V.
- Cassano et al., MultiPL-T, arXiv 2308.09895 (OOPSLA 2024 U). Python translated to Julia, Lua, OCaml, R, Racket, filtered by generated tests; fine-tuning on it beats other open code LLMs. V.
- Bannihatti Kumar et al., "Syntax Without Semantics", COLM 2026, arXiv 2605.15607. Invented language PyLang: syntax learned fast, Python still up to 19 % better; models pick the same algorithm as in Python 80 % of the time and fail to express it ("implementation fidelity gap"). V. Closest primary evidence for the chapter.
- Giagnorio, Martin-Lopez, Bavota, arXiv 2606.16827 (TSE accepted). No-resource languages (Gleam, MoonBit U): continued pretraining helps most but damages instruction following; fix = pretrain base, transfer instruct weight delta. V (few-shot > doc retrieval: U).
- Giagnorio et al., "No Silver Bullet", arXiv 2501.19085. R, Racket: fine-tuning helps small models, ICL safer for large. U.

## Checkers as feedback

- Mündler, He, Wang, Sen, Song, Vechev, "Type-Constrained Code Generation", PLDI 2025, arXiv 2504.09246. Type-constrained decoding (TypeScript) halves compile errors and raises functional correctness on synthesis, translation, repair. V (94 % of compile errors are type errors: U).
- Li, Krishnamachari, arXiv 2602.11481 (2026). GPT-5 on Idris: 22/56 zero-shot (Python 45/50); compiler-error repair loop: 54/56; compiler errors the most effective feedback. V, preprint.
- Deligiannis et al., RustAssistant, arXiv 2308.05177 (ICSE 2025 U). LLM + rustc loop fixes ~74 % of real Rust compile errors. V.
- Chen, Lin, Schärli, Zhou, "Teaching LLMs to Self-Debug", arXiv 2304.05128 (ICLR 2024 U). Execution feedback +12 % on MBPP/TransCoder; matches baselines sampling 10× more. V.
- Reading: types in the prompt did not help single-shot 2022 models (MultiPL-E); a checker in the loop does help (Mündler, Li, Deligiannis). No controlled typed-vs-untyped agentic study exists.

## Verification languages

- Bursuc et al. (incl. Tegmark), "vericoding" benchmark, arXiv 2509.22908. 12,504 specs; off-the-shelf success Dafny 82 %, Verus 44 %, Lean 27 %; pure Dafny 68 % → 96 % in one year; NL descriptions do not help. V.
- Yang et al., AutoVerus, OOPSLA 2025, arXiv 2409.13082. Verus proofs for > 90 % of 150 tasks; over half in < 30 s or ≤ 3 calls. V.
- Loughridge et al., DafnyBench, arXiv 2406.08467. 750+ programs; best model 68 % (2024). V.
- Misu, Lopes, Ma, Noble, FSE 2024, arXiv 2402.00247. GPT-4 verified Dafny 58 % with CoT + retrieval vs 19 % without context. V.
- AlgoVeri, ICML 2026 poster. Harder algorithmic specs: Gemini-3 Flash 40.3 % in Dafny. U.

## Languages for LLM authors

- Sun et al., SimPy, ISSTA 2024, arXiv 2404.16333. Formatting-free Python grammar, same AST: −13.5 % / −10.4 % tokens, accuracy kept or better. V. Only measured grammar change for LLM authors found.
- Mell et al. (incl. Zdancewic, Bastani), Quasar, COLM 2026, arXiv 2506.12202. Language for LLM code actions: logic separated from effect-tagged external calls; access control, parallelisation, conformal uncertainty. V.
- Dantanarayana et al., MTP (Jac `by`), OOPSLA 2025, arXiv 2405.08965. Languages that CALL LLMs; contrast only. U.
- No end-to-end study of a general-purpose language designed for LLM authorship.

## Generalisation vs execution

- Dziri et al., "Faith and Fate", arXiv 2305.18654 (NeurIPS 2023 U). Multiplication, logic grids, DP: linearised subgraph matching; accuracy collapses with graph depth. V.
- Anil et al., length generalisation, arXiv 2207.04901. Deficits independent of scale; scratchpad + ICL helps. V.
- Delétang et al., "Language Modeling Is Compression", arXiv 2309.10668. Chinchilla 70B beats PNG/FLAC on ImageNet patches (43.4 %) and LibriSpeech (16.4 %). V.
- Huang et al., "Compression Represents Intelligence Linearly", COLM 2024, arXiv 2404.09937. 31 LLMs: benchmark average ~linear in compression efficiency. V.
- From the site introduction: arXiv 2502.01100 (inference-time scaling does not lift the curse of complexity on logic puzzles); arXiv 2502.15776 (formalize-then-solve +65 points). Not re-verified in this search.

## Self-knowledge and enumeration

- Berglund et al., Reversal Curse, arXiv 2309.12288. GPT-4 79 % vs 33 % by direction; vanishes with the fact in context. V.
- Kadavath et al., "LMs (Mostly) Know What They Know", arXiv 2207.05221. Calibrated on formatted MC; P(IK) trainable; weak on new tasks. V.
- Yin et al., SelfAware, arXiv 2305.18153. Some self-knowledge, gap to humans. U.
- Prato et al., "Do LLMs Know How Much They Know?", EMNLP 2024, arXiv 2502.19573. At scale, models estimate the AMOUNT of their knowledge roughly correctly. V. Counterweight: the chapter's claim must be about completeness of the listed items, not about knowing the amount.
- "Mirage of Mastery", arXiv 2506.18998. > 45 % inconsistency in self-assessed feasibility under equivalent perturbations. U.
- No paper measures incompleteness of self-enumerated exhaustive lists against ground truth.

## Agent loops and tail failures

- Jimenez et al., SWE-bench, ICLR 2024, arXiv 2310.06770. 2,294 issues; Claude 2 1.96 % at publication. V.
- Bouzenia, Pradel, ASE 2025, arXiv 2506.18824. 120 trajectories: recurring action sequences and thought/action incoherence mark failures. V.
- Majgaonkar et al., arXiv 2511.00197. Failed runs longer and more variable; correct files localised in 72-81 % even when failing: the edit fails, not the search. V.
- Wang, Pradel, Liu, arXiv 2503.15223 (ICSE 2026 U). 7.8 % of accepted SWE-bench patches fail developer tests; 29.6 % of plausible patches diverge from ground truth; rates inflated 6.2 points. V. Direct evidence for uncaught tail gaps.
- Kwa et al. (METR), NeurIPS 2025, arXiv 2503.14499. 50 % time horizon doubles ~7 months since 2019; gains mainly from reliability and error recovery. V.
- Tambon et al., arXiv 2403.08937. 333 bugs; "Missing Corner Case", "Hallucinated Object", "Non-Prompted Consideration". V.
- SWE-ABS, arXiv 2603.00520. Adversarial tests: ~1 in 5 "solved" patches wrong; top agent 78.8 % → 62.2 %. U.
- SWE-agent, NeurIPS 2024, arXiv 2405.15793. U.

## Worse is Better

- Gabriel, "Lisp: Good News, Bad News, How to Win Big", EuroPAL keynote, Cambridge, March 1990; origin per https://www.dreamsongs.com/WorseIsBetter.html (V). Print: AI Expert, June 1991 (U); UNIX-HATERS Handbook reprint 1994 (U). Follow-ups (V): "Worse Is Better Is Worse" (1991, as Nickieben Bourbaki); "Is Worse Really Better?" (JOOP 1992); OOPSLA 2000 pair "Back to the Future: Is Worse (Still) Better?" / "... Worse (Still) is Better!".
- No published work links it to AI-authored code.

## Emergence

- Wei et al., "Emergent Abilities", TMLR 2022, arXiv 2206.07682. V.
- Schaeffer, Miranda, Koyejo, "Mirage", arXiv 2304.15004. Jumps come from discontinuous metrics (exact match); continuous metrics are smooth. V. Consequence for the phase-transition claim: task pass/fail is an exact-match metric; the chapter must name a continuous measure (verified outcome per token is one) or state the jump in those terms.

## Gaps (no solid publication; these are the book's own hypotheses)

- Controlled typed-vs-untyped comparison in an agentic compiler loop on matched tasks.
- End-to-end evaluation of a general-purpose language designed for LLM authors.
- Measured incompleteness of self-enumerated lists.
- Worse-is-better and AI authorship.
- Capability on a codebase jumping with its declarative share.
