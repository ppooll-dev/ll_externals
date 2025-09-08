# Building and Releasing a New Version

Instructions for maintainers preparing a new versioned release of the externals package.

---

# Developer Setup

To build and sign macOS externals, ensure you have a working codesigning environment:

```bash
git clone --recurse-submodules https://github.com/ppooll-dev/ll_externals.git
cd ll_externals
cp .env.template .env
# Edit .env with:
# - DEVELOPER_ID
# - APPLE_ID
# - TEAM_ID
# - APP_SPECIFIC_PASSWORD
```

---

# Bumping Version

To update the version number in `package-info.json`:

```bash
./scripts/bump_version.sh 1.0.0
```

This **only updates** the version field (does not commit).

---

# Building and Signing a New Release

```bash
./scripts/build_package.sh
```

This will:

* Build `.mxo` (macOS) and `.mxe64` (Windows) externals
* Sign all macOS `.mxo` externals
* Assemble `/package/` folder with documentation
* Zip as `ll_externals.zip`
* Submit for notarization
* Staple notarization ticket to zip

---

# Commit changes and tag

Commit the `package-info.json` and newly built externals.

---

# Publishing a GitHub Release

1. Go to: [GitHub Releases](https://github.com/ppooll-dev/ll_externals/releases)
2. Click **“Draft a new release”**
3. Fill out:

   * **Tag name**: `v1.0.0`
   * **Release title**: `ll_externals v1.0.0`
4. Upload the `ll_externals.zip` file
5. Click **Publish**

---

# 🧹 Cleaning Build Artifacts

After the release is published:

```bash
./scripts/clean.sh
```

This removes:

* `build-mac/`
* `build-win/`
* `package/`
* `ll_externals.zip`

---

# Notes

* Only `.mxo` files are signed and notarized (macOS only)
* Final `ll_externals.zip` must include:

  * `README.md`
  * `LICENSE.md`
  * `CHANGELOG.md`
  * `package-info.json`
