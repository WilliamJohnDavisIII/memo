# Packaging for Omarchy

`omarchy-pkgs/pkgbuilds/memo/` is laid out exactly as it goes into
[omacom/omarchy-pkgs](https://github.com/omacom/omarchy-pkgs), next to
Monologue and Hype: the PKGBUILD builds from the tagged release tarball, and
`.omarchy/package.json` watches this repo's tags so later releases sync on
their own.

To try it as Omarchy would build it:

```sh
cd packaging/omarchy-pkgs/pkgbuilds/memo
makepkg -f
```

For a new release: bump `pkgver` in both PKGBUILDs and `kVersion` in
`src/main.cpp`, tag `v<version>`, publish the release, then run
`updpkgsums` here.
