# Building the Modern OpenCV Documentation

## Prerequisites

```bash
sudo apt update
# build-essential: gcc/g++/make needed by CMake even for a docs-only build
# doxygen: generates C++ API XML consumed by Sphinx via Breathe
sudo apt install -y cmake build-essential git doxygen
```

## Clone

```bash
git clone https://github.com/Prasadayus/opencv.git
cd opencv
git fetch origin pull/27/head:pr-27
git checkout pr-27
```

## Python environment

**conda (recommended — uses the pinned `environment.yml`)**

```bash
conda env create -f doc/environment.yml
conda activate opencv_docs
```

**venv**

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r doc/requirements.txt
```

Both options install: `sphinx 8.1.3`, `pydata-sphinx-theme 0.17.1`, `myst-parser 4.0.1`,
`breathe 4.36.0`, `exhale 0.3.7`, `docutils 0.21.2`, `sphinx-autobuild`.

## Configure

**Activate the Python environment first**, then run CMake so `$(which python3)` resolves
to the env's interpreter:

```bash
mkdir build && cd build

cmake \
  -DBUILD_DOCS=ON \
  -DBUILD_DOCS_MODERN=ON \
  -DDOXYGEN_EXECUTABLE="$(which doxygen)" \
  -DPYTHON3_EXECUTABLE="$(which python3)" \
  -DPYTHON3_INCLUDE_DIR="$(python3 -c 'import sysconfig; print(sysconfig.get_path("include"))')" \
  -DPYTHON3_LIBRARY="$(python3 -c 'import sysconfig, pathlib; print(str(pathlib.Path(sysconfig.get_config_var("LIBDIR")) / ("libpython" + sysconfig.get_config_var("LDVERSION") + ".so")))')" \
  -DPYTHON3_PACKAGES_PATH="$(python3 -c 'import site; print(site.getsitepackages()[0])')" \
  ..
```

## Build

```bash
make -j$(nproc) opencv_docs_modern
```

CMake first runs the `doxygen` target (writes XML to `build/doc/doxygen/xml/`), then
Sphinx reads that XML and produces HTML.

Output: `build/doc/modern_html/index.html`

## Serve locally

Run from inside `build/`:

```bash
python3 -m http.server 8000 --directory doc/modern_html
```

Open <http://localhost:8000> in your browser.

## Troubleshooting

- **"sphinx-build not found" at configure time** — the Python env was not active when
  CMake ran. Activate it and re-run `cmake`.
- **Empty API pages** — ensure `-DBUILD_DOCS=ON` so Doxygen XML is generated before
  Sphinx runs.
