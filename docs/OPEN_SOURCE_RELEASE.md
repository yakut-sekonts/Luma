# GitHub open-source distribution

Owner decision, 2026-10-22: a full Luma release is publication in
[yakut-sekonts/Luma Releases](https://github.com/yakut-sekonts/Luma/releases).
Apple Developer Program membership, Developer ID and Apple notarization are not
required. The repository's existing license and third-party notices remain in force.

## Release contents and acceptance

- Tagged source, release notes and standalone macOS Apple Silicon DMG/ZIP.
- Bundled offline models, licenses, exact source/build identity and SHA256SUMS.
- Ad-hoc bundle signature; BUILD_INFO declares `distribution: github-open-source`,
  `signing: ad-hoc` and `notarized: false`. No Apple verification is claimed.
- Sparkle Ed25519-signed update ZIP and appcast, verified against the embedded key,
  for notification and installation through the bell.
- Completed final functional, image-quality, UX, load, recovery and artifact
  checks, with fixes and retests before publication. The older 0.5.1 exception
  does not apply to 1.0. Current status: [NOT QUALIFIED](FINAL_1_0_READINESS.md).

The publisher keeps a release as a draft until its uploaded asset set, sizes and
checksums match. It still rejects mismatched versions/commits, missing models,
missing update signatures and misleading signing metadata. Published release
assets are immutable. Optional Developer ID packaging remains available; if
selected, accepted application and DMG notarization receipts are required.

## First launch on macOS

Download from the repository's Releases page, verify SHA256SUMS, open the DMG and
copy Luma.app to Applications. Attempt to open that copy. If macOS reports that
Apple cannot verify the developer/application, dismiss the alert, then use
System Settings → Privacy & Security → Open Anyway for Luma and confirm the
per-app exception. Only approve a download whose source you trust.
See [Apple's first-launch guidance](https://support.apple.com/102445).

This is a per-app approval, not a requirement to disable Gatekeeper globally.
Neither the installer nor updater changes system security settings. A Sparkle
signature verifies the update against Luma's key; it does not provide Apple
notarization or remove the need to test the actual download/upgrade path.
