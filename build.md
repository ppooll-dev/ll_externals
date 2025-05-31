# Building and Releasing a New Version

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