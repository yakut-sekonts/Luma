# Optional Developer ID distribution — source implementation, not qualified

The owner chose open-source GitHub distribution on 2026-10-22 without an Apple
Developer Program membership. This document describes an optional future path;
it is not a prerequisite for 1.0. See [the active policy](OPEN_SOURCE_RELEASE.md).

The release packaging code now supports Developer ID signing, hardened runtime,
Apple notarization and stapled tickets for both the application and DMG. It has
not been executed. No certificate, private key or notarization credential was
retrieved, created or submitted during this implementation.

## Inputs

On a controlled local release host, provide `LUMA_SIGNING_IDENTITY` containing
the full Developer ID Application identity and `LUMA_NOTARY_PROFILE` containing
an already-configured `notarytool` Keychain profile. `LUMA_NOTARY_KEYCHAIN` is
optional for a profile stored outside the default Keychain. The scripts do not
discover or export workstation secrets.

Actions expects repository secrets `APPLE_CERTIFICATE_P12` (base64),
`APPLE_CERTIFICATE_PASSWORD`, `APPLE_NOTARY_KEY_P8`, `APPLE_NOTARY_KEY_ID` and
`APPLE_NOTARY_ISSUER_ID`, plus the non-secret repository variable
`APPLE_SIGNING_IDENTITY`. These are separate from `LUMA_SECRET` /
`SPARKLE_PRIVATE_KEY`, which sign the update archive and cannot notarize an app.
No values have been supplied or inspected by this work.

The Actions setup creates a private temporary folder and a separate Keychain,
imports the supplied certificate, stores the supplied notary API key in a named
profile, and exposes only identity/profile/Keychain-path configuration to later
steps. Temporary P12/P8 files are removed immediately. An `always()` cleanup step
restores the prior Keychain search list and removes the temporary Keychain.
Sensitive setup command output is suppressed. The setup script refuses to run
outside an Actions runner.

## Packaging sequence

`package.sh` deploys Qt, resources, source notices and build provenance first.
`sign-app.py` then signs nested Mach-O files and code bundles from the inside out,
preserving the Sparkle Downloader service's own entitlements. The main app uses
the explicit Qt JIT entitlement and hardened runtime. Signing does not use a
recursive `--deep` operation for Developer ID; deep verification remains enabled.

The script submits a temporary ZIP to Apple's notary service, requires Accepted,
staples/validates the app ticket and performs a Gatekeeper assessment. The release
ZIP is created after stapling. `release-artifacts.sh` creates the DMG, signs it,
submits it separately and staples/validates its ticket before calculating release
checksums and preparing the Sparkle appcast.

The sealed bundle's BUILD_INFO describes build identity and its required
notarization policy, without prematurely claiming acceptance. The external release
BUILD_INFO adds `notarized: true` and both submission IDs only after acceptance.
Packaging uses ad-hoc signing when no identity is configured, including 1.x.
Publication requires explicit, accurate signing/notarization metadata. If the
optional Developer ID path is selected, both Accepted receipts remain mandatory.
This does not authorize publishing an unqualified development build.

## Final qualification

Before adopting this optional path, run it with real authorized credentials, inspect
Apple's results, verify nested signatures and entitlements, and test a quarantined
download after copying from the DMG into Applications. Include Qt QML JIT,
Metal/Core ML, Sparkle's helpers, update/relaunch and offline ticket behavior.
No outcome is claimed at this source checkpoint. Signing/notarization alone does
not replace the complete functional, image, UX, recovery and load quality gate.

References:
[Sparkle signing guidance](https://sparkle-project.org/documentation/sandboxing/#code-signing),
[Qt deployment](https://doc.qt.io/qt-6/macos-deployment.html),
[Apple notarization workflow](https://developer.apple.com/documentation/security/customizing-the-notarization-workflow).
