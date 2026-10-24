# Functional completion before 1.0

User request: complete all eight functional blocks continuously, then report.
Source development only; no build, tests, static-analysis or QA execution.
Stable remains 0.5.1. All final qualification/release gates remain required.

- [x] Presence: Texture, Clarity, Dehaze.
- [x] Point Color: sampling, H/S/L adjustments and ranges, visualization.
- [x] Color Grading: four wheels, H/S/L, Blending and Balance.
- [x] Effects: Post Crop Vignette and Grain.
- [x] Smart Collections: persistent nested AND/OR/NOT conditions.
- [x] Compare: Select/Candidate with linked zoom and pan.
- [x] Auto Adjust: local estimate resolved to ordinary Develop parameters.
- [x] Advanced import: Move, date folders, naming, metadata/develop presets,
      keywords and second backup, with durable recovery and original safety.

A checked item means source workflow implemented, not qualified or released.

Implemented on `codex/1.0-development`; schema 21, preset codecs 8/9 and
processing snapshot codec 2. Details and limits: [ADR 0030](adr/0030-functional-completion.md).

No application/test builds, automated checks, static analysis, formatting checks,
QA, model conversion or release packaging were run. All eight items still need
final qualification, fixes and retests before shipping 1.0.0.
