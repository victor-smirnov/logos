# 03: Objective Plane — Needs and Emotions

## Methodological Separation
- **Objective plane:** Needs and Emotions — measurable states, observable without relying on self-report.
- **Subjective plane:** Motivations and Feelings — CCode projections into Observer's self-report.
- Folk psychology conflates these planes ("I feel hungry" fuses Need + Feeling).

## Needs
- **Definition:** Objective target functions the system tracks and optimizes.
- **Formal representation (Anokhin TFS):** Each active Need = **Acceptor of Results of Action** — continuously maintained target state compared against actual outcomes.
- Acceptor actively evaluates incoming results, computing delta (expected vs. actual satisfaction) at every step. Positive delta → reinforce current program. Negative delta → signal mismatch → reallocate resources.

### Need Taxonomy
- **A. Basal (Physiological):** Ensure organism operation. Dedicated neural substrate (hypothalamus, brainstem). Non-negotiable — failure lethal. Folk: hunger, thirst, fatigue.
- **B. Psychophysiological:** Psychological functions instrumentally linked to basal satisfaction. Criterion: activity ceases if physiological component removed. Example: working to eat.
- **C. Psychological (Ideal):** Not instrumentally linked to basal needs. Folk: passions, callings, intrinsic motivation.
- Formal structure identical across categories (Acceptor + evaluative signal + attentional steering). Differ only in Acceptor coupling.

### Need Profile
- **Hierarchy:** Weighted ordering of needs determining resource allocation.
- **Active/Passive:** Only active needs tracked and evaluated. Passive = latent, activatable by triggers.
- **Need Profile** = set of active needs + hierarchical weights. Fully determines system trajectory.
- **In LLM:** Context window contains Need Profile entirely (modulo static trained weights as background prior).

### Motivational Conflict
- Active needs **mutually contradictory** — cannot all be satisfied simultaneously under finite resources.
- System performs **multi-objective optimization**: maximize aggregate satisfaction per hierarchical weighting.
- Folk: "being torn," "mixed feelings," "dilemma."

## Emotions
- **Definition:** Signals quantitatively describing expected degree of need satisfaction, incorporating time derivative (increasing/decreasing).
- Strictly objective plane. Measurable substrate states.

### Properties
1. Indicators of **active needs only**. Passivated needs generate no emotional signals.
2. Perform **evaluative function**: describe stimulus importance to system right now.
3. **Steer attention.** Emotional signals shape the distribution of attention — frontal/lateral inhibition, orienting reflex, attentional reallocation are all driven by the emotional ensemble. Attention is never neutral; it is permanently biased by the evaluative gradient of active needs. This is how emotions translate into behavior: they determine *where the system looks next*.

### Emotional Profile
- **Additive component:** Signals from different needs summed.
- **Multiplicative component:** Hierarchical weight of need acts as multiplier.
- **Emotional Profile** = complete ensemble for a stimulus = its value to system across all active needs.

### Valence
- Positive: satisfaction increasing. Negative: satisfaction decreasing.
- Need conflict → large negative feedback (multiple Acceptors simultaneously declining).

### Emotion as Probability Distribution
- Emotional Profile ≈ probability distribution over possible actions (weighted by expected need satisfaction).
- In Transformer: literally the logit vector over vocabulary.
- Action selection = sampling + beam search over horizon.

### Emotional Decay
- **Physiological needs: cyclical.** Satisfied → decay → restart. Emotional response renewable.
- **Psychological needs: non-renewable.** Satisfied permanently. Emotional response does not restart.
- Non-renewability drives **novelty-seeking** (psychological Acceptor requires novel stimulus).
- Complex needs contain **novelty component** from psychological layer.

### Terminological Clarification
- Above = **low-level** emotions (pre-conscious evaluative signals).
- Psychology's "emotion" (anger, joy) = complex **emotional state** with entangled objective/subjective planes.
- Framework reserves "emotion" for objective signal; full complex state addressed as "Feeling" in subjective plane.

### Connections to Established Theory
- **Somatic Marker Theory (Damasio, 1994):** Low-level emotional signals constitutive for decision-making, not luxury overlay. The emotional ensemble above is the formal counterpart of somatic markers.
- **Artificial Curiosity (Schmidhuber, 2010):** Interest = first derivative of compression progress. Maps to Acceptor for predictive model accuracy. Boredom = zero compression progress.
