# ll_externals
*(klaus filip & joe steccato)*

Max/MSP externals used in [ppooll](https://github.com/ppooll-dev/ppooll) and available as a standalone Max package.

> ⚡ **Note:**  
> If you just want to use [ppooll](https://github.com/ppooll-dev/ppooll), you do **not** need to download or install ll_externals separately — it is already bundled inside ppooll.

---

## Included Externals

- **ll_number** – GUI number and (multi)slider in one box
- **ll_mcwaveform** – GUI waveform for multichannel soundfiles
- **ll_2dslider** – GUI array of dots
- **ll_fastforward** – Send to the header of a message
- **ll_slishi** – GUI slider with three levels of control

---

## Installation (for Max Users)

If you're a **regular Max user**, you can:

1. Download the latest [ll_externals.zip release](https://github.com/ppooll-dev/ll_externals/releases).
2. Unzip it and move the `ll_externals` folder into:
   - macOS: `~/Documents/Max 8/Packages`
   - Windows: `My Documents\Max 8\Packages`
3. Restart Max.

✅ Done — the externals will be available inside Max.

---

# 🚀 Building and Releasing a New Version

This section is for **maintainers** preparing an official new release.

## Quick Summary:

1. Build the package manually
2. Verify build output
3. Bump version and tag release
4. Push to GitHub
5. Upload the zip
6. (Optional) Clean build artifacts

---

## Detailed Steps:

### 1. Build the Release Package

```bash
./scripts/build_sign_package.sh --platform=all
```

This will:
- Build macOS `.mxo` externals
- Build Windows `.mxe64` externals
- Code-sign and notarize macOS externals
- Assemble a clean `/package/` folder
- Create `ll_externals.zip` in the repo root

### 2. Verify

- Check that `ll_externals.zip` was created.
- Ensure externals, docs, and help patches are correct.

### 3. Bump Version and Tag Release

After verifying the build:

```bash
./scripts/release.sh 0.9.0
```

This will:
- Update the version number in `package-info.json`
- Commit the change
- Create a Git tag

### 4. Push to GitHub

```bash
git push
git push --tags
```

### 5. Upload the Release to GitHub

- Go to the [Releases page](https://github.com/ppooll-dev/ll_externals/releases)
- Click **"Draft a new release"**
- Set:
  - **Tag:** e.g., `v1.0.0`
  - **Release title:** e.g., `ll_externals v1.0.0`
- **Attach the `ll_externals.zip` file**
- Click **Publish**.

### 6. (Optional) Clean the Repo After Release

```bash
./scripts/clean.sh
```

This deletes:
- `build-mac/`
- `build-win/`
- `package/`
- `ll_externals.zip`

---

# 🧠 Important Notes

- **package-info.json** defines the Max package metadata and must match the version.
- **Help patches** must exist under `/help/`.
- **Docs (.maxref.xml)** must exist under `/docs/`.
- **Do not delete** the `/source/`, `/scripts/`, `/toolchains/`, or `/max-sdk-base/` folders unless necessary.

---

# 🛠 Development Manual

Clone the repository including submodules:

```bash
git clone --recurse-submodules https://github.com/ppooll-dev/ll_externals.git
cd ll_externals
```

Setup your environment:

```bash
cp .env.template .env
# edit your .env file to set developer ID and notarization profile
```

Manually build if needed:

#### macOS:

```bash
mkdir build-mac
cd build-mac
cmake ..
cmake --build .
```

#### Windows (cross-compilation):

```bash
brew install mingw-w64
mkdir build-win
cd build-win
cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchains/WindowsToolchain.cmake
cmake --build .
```

---

# License

See [LICENSE](LICENSE).

---

# Website

[ppooll.klingt.org](http://ppooll.klingt.org)
