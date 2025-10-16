# ll_externals

_(klaus filip & joe steccato)_

Max/MSP externals used in [ppooll](https://github.com/ppooll-dev/ppooll) and available as a standalone Max package.

> **Note:**  
> If you just want to use [ppooll](https://github.com/ppooll-dev/ppooll), you do **not** need to download or install ll_externals separately — it is already bundled inside ppooll.

---

## Included Externals

-   **ll_number** – GUI number and (multi)slider in one box
-   **ll_mcwaveform** – GUI waveform for multichannel soundfiles
-   **ll_2dslider** – GUI array of dots
-   **ll_fastforward** – Send to the header of a message
-   **ll_slishi** – GUI slider with three levels of control
-   **ll_filewatchers** – watch many files & folders

---

## Installation (for Max Users)

1. Download the latest [ll_externals.zip release](https://github.com/ppooll-dev/ll_externals/releases).
2. Unzip it and move the `ll_externals` folder into:
    - macOS: `~/Documents/Max 8/Packages`
    - Windows: `My Documents\Max 8\Packages`
3. Restart Max.

---

# Development

Clone the repository including submodules into:

-   macOS: `~/Documents/Max 8/Packages`
-   Windows: `My Documents\Max 8\Packages`

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
