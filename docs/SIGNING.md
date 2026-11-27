# Code signing (Windows)

The Windows programs and installers are signed through the free
**SignPath Foundation** programme for open-source projects: SignPath
keeps the certificate (issued to *SignPath Foundation*) in its hardware
module, and signs what the GitHub Actions build of this repository sends
it, after a manual approval. Nobody, the maintainer included, holds the
private key; only a build of this repository, on GitHub's runners, can
be signed.

The policy shown to the users is the *Code signing policy* section of
the [README](../README.md#code-signing-policy).

## What is signed

On a **published Release** only (not on the pushes and pull requests),
in two signing requests, each approved by hand on signpath.io:

| Request | CI artifact | SignPath artifact configuration | Files |
|---|---|---|---|
| 1 | `unsigned-binaries` | `binaries` ([`installer/signpath/binaries.xml`](../installer/signpath/binaries.xml)) | `Twist.exe`, `Twist.dll`, `twistd.exe`, `operad.exe`, `pi4d.exe`, `twistenc.exe`, `twistlive.exe`, `libtwcore.dll`, `libtwist.dll`, `libopera.dll`, `libpi4.dll` |
| 2 | `unsigned-installers` | `installers` ([`installer/signpath/installers.xml`](../installer/signpath/installers.xml)) | `TWIST-Console-<version>-setup.exe`, `TWIST-Console-<version>-mise-a-jour.exe` |

Request 1 comes before the installers are built, so they carry the
signed files; request 2 before `latest.json`, so its SHA-256 are those
of the signed installers. The third-party DLLs (FFTW, PortAudio, Hamlib,
.NET, Avalonia) are not ours and are not signed by us. The uninstaller
that Inno Setup writes (`unins000.exe`) is not signed.

The CI checks each signed file (`Get-AuthenticodeSignature`: *Valid*)
and stops otherwise. While the secret below does not exist, these steps
are skipped and the Release is built unsigned, as before; its text says
which.

## Setting it up (once)

1. **Apply** at <https://signpath.org/apply> (open-source project,
   GitHub repository `f6fbb/TWIST`, GPL v3, Release builds on GitHub
   Actions). Conditions: an OSI licence, no malware or "potentially
   unwanted" behaviour, the project already released, the code signing
   policy published (README), multi-factor authentication for the
   people with write access to the repository and to SignPath.
2. Once accepted, in **SignPath** (app.signpath.io):
   - the project with the slug **`twist-console`**, linked to the
     repository through the *GitHub* trusted build system;
   - two artifact configurations, slugs **`binaries`** and
     **`installers`**, from the two XML files of `installer/signpath/`;
   - the signing policy **`release-signing`** (the Foundation's
     certificate), with the maintainer as approver;
   - an API token for a CI user (*Submitter* on that policy).
3. In **GitHub**, repository *Settings › Secrets and variables ›
   Actions*:
   - secret **`SIGNPATH_API_TOKEN`**: the API token;
   - variable **`SIGNPATH_ORGANIZATION_ID`**: the organization ID shown
     in SignPath.
4. Install the *SignPath* GitHub App on the repository, if SignPath asks
   for it.

## Each Release

Publish the Release as before. The `windows` job then waits (up to two
hours per request) for the approvals on signpath.io: request 1, then
request 2 a few minutes later. Once both are approved, the signed
installers are added to the Release.

SmartScreen judges a certificate by its reputation: during the first
versions it may still show a warning, less and less as the signed
installers are downloaded.
