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

1. Build the package
2. Upload the zip to GitHub
3. (Optional) Tag the release
4. (Optional) Clean build artifacts

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

---

### 2. Upload the Release to GitHub

- Go to the [Releases page](https://github.com/ppooll-dev/ll_externals/releases)
- Click **"Draft a new release"**
- Set:
  - **Tag:** e.g., `v1.0.0`
  - **Release title:** e.g., `ll_externals v1.0.0`
- **Attach the `ll_externals.zip` file** as the release asset.
- Click **Publish**.

---

### 3. (Optional) Bump Version and Tag Automatically

If you want to automate updating `package-info.json` and creating a Git tag:

```bash
./scripts/release.sh 1.1.0
git push
git push --tags
```

---

### 4. (Optional) Clean the Repo After Release

```bash
./scripts/clean.sh
```

This deletes:
- `build-mac/`
- `build-win/`
- `externals/`
- `package/`
- `ll_externals.zip`

---

# 🧠 Important Notes

- **package-info.json** defines the Max package metadata and must match the version.
- **Help patches** must exist under `/help/`.
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
