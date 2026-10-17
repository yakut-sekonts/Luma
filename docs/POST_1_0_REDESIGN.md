# Frontend redesign after 1.0

User decision, 2026-10-17: redesign Luma's frontend after the qualified 1.0 release.

Use these sources when that stage begins:

- [Impeccable](https://github.com/pbakaus/impeccable): product context, design
  critique, hierarchy, typography, spacing, reusable visual rules, accessibility,
  localization resilience and a final visual review.
- [Taste Skill](https://github.com/Leonxlnx/taste-skill): reference-led art
  direction and iterative visual review. Its existing-project redesign and
  product-interface guidance are relevant to Luma; the default public marketing
  page workflow is not a specification for the photo editor.

Apply the design principles to the existing Qt/QML macOS application. These
references do not change the current implementation stack or phase order.
Choose the concrete visual direction when the redesign starts, using the actual
Library, Develop, presets, masks, export, settings and update workflows as context.

Audit the existing interface first. Develop shared visual rules and screen
proposals, then implement and inspect them in the native app across supported
window sizes and all eight language packs. Preserve editing, history,
accessibility, keyboard navigation, catalog and original-file contracts.
Test the redesign, fix regressions and only then publish it under the existing
release qualification policy. Re-read and pin the relevant upstream guidance
at that stage; no external install scripts or hooks were introduced by this note.
