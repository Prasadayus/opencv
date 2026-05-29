"""Sphinx wrapper for opencv/doc/.

The wrapper lives in opencv/docs_sphinx/ as a single conf.py. Sphinx is
invoked with config-dir / source-dir separation so the wrapper never
duplicates the legacy tree. Build via the CMake `sphinx` target:

    cmake --build <build> --target sphinx
    # output -> <build>/docs_sphinx/html/

opencv/doc/ stays untouched: Doxygen-flavored directives in the .markdown
sources are translated to MyST in a `source-read` hook below.

To enable additional tutorial modules, append their directory names (the
folder under opencv/doc/tutorials/) to DOC_MODULES below. The root index
(tutorials/tutorials.markdown) lists every module, but only modules in
DOC_MODULES are actually compiled — entries for the rest are dropped
from toctrees automatically.
"""

from __future__ import annotations
import pathlib, re

HERE = pathlib.Path(__file__).parent.resolve()
DOC_ROOT = (HERE.parent / "doc").resolve()
OPENCV_ROOT = HERE.parent.resolve()

# ---------------------------------------------------------------------------
# SCOPE — add module folder names from opencv/doc/tutorials/ here.
# Override via env var to avoid editing this file:
#     OPENCV_DOC_MODULES=photo,imgproc cmake --build <build> --target sphinx
# ---------------------------------------------------------------------------
import os as _os
DOC_MODULES = [
    m.strip()
    for m in (_os.environ.get("OPENCV_DOC_MODULES") or "photo,objdetect,imgproc,3d,app,ios").split(",")
    if m.strip()
]
DOC_JS_MODULES = [
    m.strip()
    for m in (_os.environ.get("OPENCV_DOC_JS_MODULES") or "js_gui,js_core,js_imgproc,js_video,js_dnn").split(",")
    if m.strip()
]

DOC_PY_MODULES = [
    m.strip()
    for m in (_os.environ.get("OPENCV_DOC_PY_MODULES") or "py_gui,py_features,py_calib3d,py_ml,py_bindings").split(",")
    if m.strip()
]

# ---------------------------------------------------------------------------
# SCOPE — contrib modules under opencv_contrib/modules/.
# Override via env var:  OPENCV_CONTRIB_MODULES=sfm cmake --build <build> --target sphinx
# ---------------------------------------------------------------------------
CONTRIB_ROOT = pathlib.Path(
    _os.environ.get("OPENCV_CONTRIB_ROOT",
                    str(HERE.parent.parent / "opencv_contrib" / "modules"))
).resolve()
CONTRIB_MODULES = [
    m.strip()
    for m in (_os.environ.get("OPENCV_CONTRIB_MODULES") or "sfm").split(",")
    if m.strip()
]

# ---------------------------------------------------------------------------
# SCOPE — API reference. Module folder names under opencv/modules/. Each
# entry's umbrella header (modules/<m>/include/opencv2/<m>.hpp) must declare
# `@defgroup <m> …` at the top — that's the breathe target. Override:
#     OPENCV_API_MODULES=core,imgproc cmake --build <build> --target sphinx
# Empty = legacy behavior (no API pages in Sphinx; navbar's external_links
# still routes users to the Doxygen-rendered group__*.html).
# ---------------------------------------------------------------------------
API_MODULES = [
    m.strip()
    for m in (_os.environ.get("OPENCV_API_MODULES") or "core").split(",")
    if m.strip()
]

# Sphinx srcdir as seen by conf.py.  CMake stages a merged tree at
# ${CMAKE_BINARY_DIR}/docs_sphinx_input/ and forwards this env var.
# Default = DOC_ROOT so ad-hoc sphinx-build runs keep working. The `or`
# idiom (rather than dict.get's default) treats an empty-string env var
# the same as unset — CMake forwards "" when contrib is disabled.
SPHINX_INPUT_ROOT = pathlib.Path(
    _os.environ.get("OPENCV_SPHINX_INPUT_ROOT") or str(DOC_ROOT)
).resolve()

# Expose enabled contrib modules via symlinks + html_extra_path.
# Output URLs: /contrib_modules/<m>/... — no duplication in srcdir.
html_extra_path: list[str] = []
def _in_source_tree(p: pathlib.Path) -> bool:
    for _root in (DOC_ROOT, CONTRIB_ROOT):
        try:
            p.relative_to(_root)
            return True
        except ValueError:
            pass
    return False

if not _in_source_tree(SPHINX_INPUT_ROOT):
    _extras = SPHINX_INPUT_ROOT.parent / "contrib_extras"
    _prefix = _extras / "contrib_modules"
    _prefix.mkdir(parents=True, exist_ok=True)
    for _m in CONTRIB_MODULES:
        _src, _link = CONTRIB_ROOT / _m, _prefix / _m
        if _src.is_dir() and not _link.exists():
            try:
                _os.symlink(_src, _link, target_is_directory=True)
            except (OSError, NotImplementedError):
                pass
    html_extra_path = [str(_extras)]

# -- Project ----------------------------------------------------------------
project = "OpenCV"
author = "OpenCV Team"
release = "5.x"

# -- Sphinx core ------------------------------------------------------------
extensions = ["myst_parser"]
for _ext in ("sphinx_design", "sphinx_copybutton"):
    try:
        __import__(_ext)
        extensions.append(_ext)
    except ImportError:
        pass
HAVE_SPHINX_DESIGN = "sphinx_design" in extensions

# -- Breathe (Doxygen XML -> Sphinx C++ domain) -----------------------------
# Gated on API_MODULES being non-empty AND breathe being importable. A
# stripped env (no breathe) or empty API_MODULES degrades to tutorial-only,
# matching today's pre-API behavior. XML dir is forwarded by
# docs_sphinx/CMakeLists.txt via OPENCV_DOXYGEN_XML_DIR; fallback path
# matches the canonical build_doc layout for ad-hoc sphinx-build runs.
_API_XML_DIR = pathlib.Path(
    _os.environ.get("OPENCV_DOXYGEN_XML_DIR")
    or str(HERE.parent.parent / "build_doc" / "doc" / "doxygen" / "xml")
).resolve()
# Patched XML dir (see `_patch_namespace_xml_for_breathe` below). breathe is
# pointed at this rather than the raw Doxygen output so that functions defined
# inside `@addtogroup` regions (which Doxygen lists only as `<member refid>`
# in namespace XML, not full `<memberdef>`) become findable. The dir is built
# at sphinx-build time, mirrors the original XML via symlinks, and only the
# affected namespace XMLs are rewritten in place.
_PATCHED_XML_DIR = _API_XML_DIR.parent / "xml_for_sphinx"

# Python enum/constant signatures; built by: cmake --build --target gen_opencv_python_source
_PY_SIGNATURES: dict = {}
for _pysigs_candidate in [
    _API_XML_DIR.parents[2] / "modules/python_bindings_generator/pyopencv_signatures.json",
    _os.environ.get("OPENCV_PYTHON_SIGNATURES_FILE", ""),
]:
    _pysigs_path = pathlib.Path(str(_pysigs_candidate)) if _pysigs_candidate else None
    if _pysigs_path and _pysigs_path.is_file():
        import json as _json
        _PY_SIGNATURES = _json.loads(_pysigs_path.read_text(encoding="utf-8"))
        break
del _pysigs_candidate, _pysigs_path

if API_MODULES:
    try:
        import breathe  # noqa: F401
        extensions.append("breathe")
        breathe_projects = {"opencv": str(_PATCHED_XML_DIR)}
        breathe_default_project = "opencv"
        breathe_default_members = ("members",)
        # Breathe 4.36 doesn't handle optional title in Doxygen 1.12 docSect2TypeSub
        from breathe.renderer import sphinxrenderer as _bsr
        _orig_visit = _bsr.SphinxRenderer.methods["docsect1"]
        def _visit_docsectN(self, node):
            if not getattr(node, "title", None):
                return self.render_iterable(node.content_)
            return _orig_visit(self, node)
        _bsr.SphinxRenderer.methods["docsect1"] = _visit_docsectN
        _bsr.SphinxRenderer.methods["docsect2"] = _visit_docsectN
        _bsr.SphinxRenderer.methods["docsect3"] = _visit_docsectN
    except ImportError:
        API_MODULES = []

source_suffix = {".md": "markdown", ".markdown": "markdown"}

# Tell Sphinx's C/C++ domain parser to treat OpenCV's compatibility macros
# as identifier attributes (i.e. swallow them silently during parsing).
# Without this, signatures like
#     inline virtual const char *getName() const CV_OVERRIDE
# raise "Invalid C++ declaration: Expected end of definition" because the
# parser sees CV_OVERRIDE as an unknown token after `const`. These macros
# expand to `override` / `noexcept` / `final` / nothing in cvdef.h.
cpp_id_attributes = [
    "CV_OVERRIDE", "CV_FINAL", "CV_NOEXCEPT",
    "CV_NORETURN", "CV_DEPRECATED", "CV_DEPRECATED_EXTERNAL",
    "CV_NODISCARD_STD", "CV_NODISCARD",
    "CV_EXPORTS", "CV_EXPORTS_W",
    "CV_WRAP",
]
c_id_attributes = list(cpp_id_attributes)

# Root tutorial index (lists all modules via @subpage). Stays the master
# regardless of how many modules are in DOC_MODULES.
master_doc = "tutorials/tutorials"

# Source dir is opencv/doc/ — scope to the master + enabled modules only.
include_patterns = ["tutorials/tutorials.markdown"] + [
    f"tutorials/{m}/**" for m in DOC_MODULES
] + (["js_tutorials/js_tutorials.markdown"] + [
    f"js_tutorials/{m}/**" for m in DOC_JS_MODULES
] if DOC_JS_MODULES else []) + (["py_tutorials/py_tutorials.markdown"] + [
    f"py_tutorials/{m}/**" for m in DOC_PY_MODULES
] if DOC_PY_MODULES else []) + (["tutorials_contrib/contrib_root.markdown"] + [
    f"tutorials_contrib/{m}/**" for m in CONTRIB_MODULES
] if CONTRIB_MODULES else [])

# Add API stubs if API_MODULES is defined by the cherry-picked commit
if API_MODULES:
    include_patterns.append("api/**")

exclude_patterns = ["**/Thumbs.db", "**/.DS_Store", "tutorials/app/_old/**"]

myst_enable_extensions = [
    "colon_fence", "deflist", "dollarmath", "amsmath",
    "attrs_inline", "attrs_block", "smartquotes",
]
myst_heading_anchors = 4
suppress_warnings = [
    "myst.header", "myst.xref_missing", "toc.not_included",
    "misc.highlighting_failure",
    "image.not_readable",
    "cpp.duplicate_declaration",
]

# -- Doxygen integration -----------------------------------------------------
# External links in the navbar and unbuilt-module sidebar entries point at
# the existing Doxygen build. Override the base URL or tagfile via env vars.
DOXYGEN_BASE_URL = (
    _os.environ.get("OPENCV_DOXYGEN_BASE_URL", "https://docs.opencv.org/5.x/")
    .rstrip("/") + "/")
_TAG_FILE = pathlib.Path(_os.environ.get(
    "OPENCV_DOXYGEN_TAGFILE",
    str(HERE.parent.parent / "build" / "doc" / "doxygen" / "html" / "opencv.tag"),
))
if not _TAG_FILE.is_file():
    _TAG_FILE = HERE.parent.parent / "build" / "doc" / "opencv.tag"

# anchor -> doxygen URL filename (from opencv.tag if available).
_TAG_FILENAMES: dict[str, str] = {}
# anchor -> human-readable page title (from opencv.tag).
_TAG_PAGE_TITLES: dict[str, str] = {}
# cv API name -> full doxygen URL (for cv.Name auto-linking).
_CV_API: dict[str, str] = {}
if _TAG_FILE.is_file():
    try:
        import xml.etree.ElementTree as _ET
        for _c in _ET.parse(str(_TAG_FILE)).getroot().iter("compound"):
            if _c.get("kind") == "page":
                _n, _f = _c.findtext("name"), _c.findtext("filename")
                _t = _c.findtext("title", "")
                if _n and _f:
                    _TAG_FILENAMES[_n] = _f if _f.endswith(".html") else _f + ".html"
                    if _t:
                        _TAG_PAGE_TITLES[_n] = _t
            if _c.get("kind") == "class":
                _cn = (_c.findtext("name") or "").split("::")[-1]
                _cf = _c.findtext("filename", "")
                if _cn and _cf:
                    _CV_API.setdefault(_cn, DOXYGEN_BASE_URL + (_cf if _cf.endswith(".html") else _cf + ".html"))
            for _m in _c.findall("member"):
                _n = _m.findtext("name", "")
                _af = _m.findtext("anchorfile", "")
                _an = _m.findtext("anchor", "")
                if _n and _af and _an:
                    _CV_API.setdefault(_n, DOXYGEN_BASE_URL + _af + "#" + _an)
    except Exception:
        pass

def _doxygen_url(page: str) -> str:
    return DOXYGEN_BASE_URL + _TAG_FILENAMES.get(page, page)

# -- HTML / PyData theme ----------------------------------------------------
try:
    import pydata_sphinx_theme  # noqa: F401
    html_theme = "pydata_sphinx_theme"
except ImportError:
    html_theme = "alabaster"

html_title = "OpenCV Tutorials"
html_show_sourcelink = False
templates_path = ["_templates"]
html_static_path = ["_static"]
html_css_files = [
    "https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700"
    "&family=JetBrains+Mono:wght@400;500&display=swap",
    "custom.css",
]
html_theme_options = {
    "logo": {"text": f"OpenCV {release}"},
    # Show all 7 Doxygen-style external links inline (no "More" dropdown).
    "header_links_before_dropdown": 7,
    # Doxygen-style top-level nav (the legacy site's MAIN PAGE / RELATED
    # PAGES / NAMESPACES / CLASSES / FILES / EXAMPLES / JAVA DOCUMENTATION).
    # All external — they target the existing Doxygen build.
    "external_links": [
        {"url": _doxygen_url("index.html"),       "name": "Main Page"},
        {"url": _doxygen_url("pages.html"),       "name": "Related Pages"},
        {"url": _doxygen_url("namespaces.html"),  "name": "Namespaces"},
        {"url": _doxygen_url("annotated.html"),   "name": "Classes"},
        {"url": _doxygen_url("files.html"),       "name": "Files"},
        {"url": _doxygen_url("examples.html"),    "name": "Examples"},
        {"url": DOXYGEN_BASE_URL + "javadoc/",    "name": "Java Documentation"},
    ],
    "show_toc_level": 2,
    "navigation_with_keys": True,
    "show_prev_next": True,
    "show_nav_level": 2,
    "navigation_depth": 4,
    "secondary_sidebar_items": ["page-toc"],
    "back_to_top_button": True,
    "show_version_warning_banner": False,
    "icon_links": [{"name": "GitHub",
                    "url": "https://github.com/opencv/opencv",
                    "icon": "fa-brands fa-github"}],
}

# Doxygen defines \fork{a}{b}{c}{d} as a piecewise-function shorthand.
# Define it as a MathJax macro so threshold.markdown renders correctly.
mathjax3_config = {
    "tex": {
        "macros": {
            "fork": [r"\left\{ \begin{array}{ll} #1 & \mbox{#2}\\ #3 & \mbox{#4}\end{array} \right.", 4],
        }
    }
}

# Exposed to templates/navbar-nav.html so it can rewrite external_links
# whose URL starts with this base into depth-aware relative paths to the
# local Doxygen output, instead of redirecting users to docs.opencv.org.
html_context = {"doxygen_base_url": DOXYGEN_BASE_URL}

# ===========================================================================
#  Doxygen-flavored .markdown  ->  MyST translation via source-read.
#  Nothing on disk under opencv/doc/ is modified.
# ===========================================================================

# Build anchor maps. Two kinds:
#   _ANCHOR_TO_DOC      anchor -> docname  (internal — for enabled modules)
#   _ANCHOR_TO_EXTERNAL anchor -> (title, url)  (external — for the rest)
# Disabled modules still appear in the master toctree as external links to
# the Doxygen build, so the left sidebar shows the full module list.
_ANCHOR_TO_DOC: dict[str, str] = {}
_ANCHOR_TO_EXTERNAL: dict[str, tuple[str, str]] = {}

_HEAD_RE = re.compile(
    r"^(?P<title1>[^\n]+?)\s*\{#(?P<anchor1>[\w-]+)\}\s*\n[=\-]{3,}\s*$"
    r"|"
    r"^#+\s+(?P<title2>[^\n]+?)\s*\{#(?P<anchor2>[\w-]+)\}\s*$",
    re.MULTILINE)

def _scan_internal(path: pathlib.Path, base: pathlib.Path | None = None) -> None:
    """Add every {#anchor} and standalone `@anchor NAME` in `path` (file or
    dir) to _ANCHOR_TO_DOC. Docname is computed relative to `base` (default
    SPHINX_INPUT_ROOT) so the same scanner serves both main and contrib."""
    base = base or SPHINX_INPUT_ROOT
    _md_exts = (".markdown", ".md")
    files = [path] if (path.is_file() and path.suffix in _md_exts) \
        else (list(path.rglob("*.markdown")) + list(path.rglob("*.md"))
              if path.is_dir() else [])
    for md in files:
        try:
            head = md.read_text(encoding="utf-8", errors="replace")[:4000]
        except OSError:
            continue
        rel = md.relative_to(base).with_suffix("").as_posix()
        for m in re.finditer(r"\{#([\w-]+)\}", head):
            _ANCHOR_TO_DOC[m.group(1)] = rel

def _scan_external(toc_file: pathlib.Path) -> None:
    """Pull the top heading's (title, anchor) from a module's table_of_content
    file and add it to _ANCHOR_TO_EXTERNAL with a URL into the Doxygen build."""
    try:
        head = toc_file.read_text(encoding="utf-8", errors="replace")[:4000]
    except OSError:
        return
    m = _HEAD_RE.search(head)
    if not m:
        return
    anchor = m.group("anchor1") or m.group("anchor2")
    title = (m.group("title1") or m.group("title2") or "").strip()
    if not anchor:
        return
    url = DOXYGEN_BASE_URL + _TAG_FILENAMES.get(anchor, "index.html")
    _ANCHOR_TO_EXTERNAL[anchor] = (title, url)

# Internal scan: master + every enabled module subtree.
_scan_internal(DOC_ROOT / "tutorials" / "tutorials.markdown", base=DOC_ROOT)
for _m in DOC_MODULES:
    _scan_internal(SPHINX_INPUT_ROOT / "tutorials" / _m)
_contrib_root_md = SPHINX_INPUT_ROOT / "tutorials_contrib" / "contrib_root.markdown"
if _contrib_root_md.is_file():
    _scan_internal(_contrib_root_md)
for _m in CONTRIB_MODULES:
    _scan_internal(SPHINX_INPUT_ROOT / "tutorials_contrib" / _m)
# Generate the API stub tree from Doxygen XML, then scan it. Stub layout
# mirrors Doxygen's group hierarchy: parent groups (those with <innergroup>
# children in their XML) become navigation index pages with `@subpage`
# toctrees; leaf groups get a single `{doxygengroup} <name>` directive.
# This matches what docs.opencv.org/.../group__core.html does — Doxygen
# also separates navigation from content; only one breathe directive
# (`{doxygengroup} core :inner:`) flattens the whole hierarchy onto one
# page, which is why we don't use it.
def _itertext(el) -> str:
    """Flatten an XML element's inner text. None-safe."""
    return "".join(el.itertext()).strip() if el is not None else ""


def _type_to_md(type_elem) -> str:
    """Render <type> XML as markdown; turns <ref> children into links."""
    if type_elem is None:
        return ""
    out = type_elem.text or ""
    for child in type_elem:
        if child.tag == "ref":
            word = (child.text or "").strip()
            refid = child.get("refid", "")
            kindref = child.get("kindref", "")
            if kindref == "compound" and refid in _ANCHOR_TO_DOC:
                fn = _ANCHOR_TO_DOC[refid].split("/")[-1]
                out += f"[{word}]({fn}.md)"
            elif refid:
                out += f"[{word}](#{refid})"
            else:
                out += word
        out += child.tail or ""
    return out.strip()


# memberdef@kind → display section title. Mirrors Doxygen's group-page order.
_MEMBERDEF_SECTIONS = (
    ("typedef",  "Typedefs"),
    ("enum",     "Enumerations"),
    ("function", "Functions"),
    ("variable", "Variables"),
    ("define",   "Macros"),
)


def _read_class_brief(refid: str, xml_dir: pathlib.Path,
                      _cache: dict = {}) -> str:
    """Read brief description from a class/struct's compound XML. Cached."""
    if refid in _cache:
        return _cache[refid]
    import xml.etree.ElementTree as _ET
    xml_path = xml_dir / f"{refid}.xml"
    brief = ""
    if xml_path.is_file():
        try:
            ccd = _ET.parse(xml_path).getroot().find("compounddef")
            if ccd is not None:
                brief = _itertext(ccd.find("briefdescription"))
        except _ET.ParseError:
            pass
    _cache[refid] = brief
    return brief


def _doxygen_desc_to_md(el, h_level: int = 3) -> str:
    """Convert a Doxygen <detaileddescription> (or similar) element to Markdown."""
    if el is None:
        return ""

    def _hl_text(hl_node) -> str:
        """Extract text from a <highlight> element, treating <sp/> as a space."""
        parts = []
        if hl_node.text:
            parts.append(hl_node.text)
        for child in hl_node:
            if child.tag == "sp":
                parts.append(" ")
            elif child.tag == "ref":
                parts.append("".join(child.itertext()))
            else:
                parts.append("".join(child.itertext()))
            if child.tail:
                parts.append(child.tail)
        return "".join(parts)

    def _programlisting(node) -> str:
        lines = []
        for codeline in node.findall("codeline"):
            lines.append("".join(_hl_text(hl) for hl in codeline.findall("highlight")))
        return "```cpp\n" + "\n".join(lines) + "\n```"

    def _inline(node) -> str:
        """Render inline content of a node (inline elements only)."""
        parts = []
        if node.text:
            parts.append(node.text)
        for child in node:
            t = child.tag
            if t in _BLOCK_TAGS:
                break  # stop at block-level — caller handles separately
            inner = "".join(child.itertext())
            if t == "ulink":
                url = child.get("url", "")
                parts.append(f"[{inner}]({url})" if url else inner)
            elif t == "ref":
                parts.append(_ref_link(child.get("refid", ""), inner))
            elif t == "computeroutput":
                parts.append(f"`{inner}`" if inner else "")
            elif t == "emphasis":
                parts.append(f"*{inner}*" if inner else "")
            elif t in ("bold", "strong"):
                parts.append(f"**{inner}**" if inner else "")
            elif t == "sp":
                parts.append(" ")
            elif t == "linebreak":
                parts.append("\n")
            else:
                parts.append(inner)
            if child.tail:
                parts.append(child.tail)
        return "".join(parts)

    _BLOCK_TAGS = {"orderedlist", "itemizedlist", "programlisting", "simplesect"}

    def _ref_link(refid: str, text: str) -> str:
        if not (refid and text):
            return f"`{text}`" if text else ""
        import re as _re
        m = _re.search(r'_1([a-z]{1,3}[0-9a-f]{20,})$', refid)
        if m:
            url = f"{DOXYGEN_BASE_URL}{refid[:m.start()]}.html#{m.group(1)}"
        else:
            url = f"{DOXYGEN_BASE_URL}{refid}.html"
        return f"[`{text}`]({url})"

    def _listitem_text(item) -> str:
        parts = []
        for child in item:
            if child.tag == "para":
                parts.append(_inline(child).strip())
        return " ".join(p for p in parts if p)

    def _emit_block(sub, result: list, level: int) -> None:
        t = sub.tag
        if t == "programlisting":
            result.append(_programlisting(sub))
        elif t == "orderedlist":
            for i, item in enumerate(sub.findall("listitem"), 1):
                result.append(f"{i}. {_listitem_text(item)}")
        elif t == "itemizedlist":
            for item in sub.findall("listitem"):
                result.append(f"- {_listitem_text(item)}")
        elif t == "simplesect":
            kind = sub.get("kind", "")
            admon = {"note": "note", "warning": "warning",
                     "attention": "warning", "remark": "note"}.get(kind)
            body = "\n\n".join(_blocks(sub, level))
            if admon:
                result.append(f":::{{{admon}}}\n{body}\n:::")
            elif body:
                result.append(body)

    def _blocks(node, level: int) -> list[str]:
        result = []
        for child in node:
            t = child.tag
            if t == "title":
                continue
            elif t == "para":
                # Walk para children; flush text before each block-level child.
                pending: list[str] = []
                if child.text:
                    pending.append(child.text)
                for sub in child:
                    if sub.tag in _BLOCK_TAGS:
                        text = "".join(pending).strip()
                        if text:
                            result.append(text)
                        pending = []
                        _emit_block(sub, result, level)
                        if sub.tail and sub.tail.strip():
                            pending.append(sub.tail)
                    else:
                        inner = "".join(sub.itertext())
                        st = sub.tag
                        if st == "ulink":
                            url = sub.get("url", "")
                            pending.append(f"[{inner}]({url})" if url else inner)
                        elif st == "ref":
                            pending.append(_ref_link(sub.get("refid", ""), inner))
                        elif st == "computeroutput":
                            pending.append(f"`{inner}`" if inner else "")
                        elif st == "emphasis":
                            pending.append(f"*{inner}*" if inner else "")
                        elif st in ("bold", "strong"):
                            pending.append(f"**{inner}**" if inner else "")
                        elif st == "sp":
                            pending.append(" ")
                        elif st == "linebreak":
                            pending.append("\n")
                        else:
                            pending.append(inner)
                        if sub.tail:
                            pending.append(sub.tail)
                text = "".join(pending).strip()
                if text:
                    result.append(text)
            elif t in ("sect1", "sect2", "sect3"):
                title_text = child.findtext("title") or ""
                if title_text:
                    result.append(f"{'#' * level} {title_text}")
                    result.extend(_blocks(child, level + 1))
                else:
                    result.extend(_blocks(child, level))
            elif t in _BLOCK_TAGS:
                _emit_block(child, result, level)
        return result

    return "\n\n".join(b for b in _blocks(el, h_level) if b.strip())


def _build_api_hierarchy(refid: str, xml_dir: pathlib.Path,
                         _seen: set | None = None) -> dict | None:
    """Walk a group XML's <innergroup> children recursively.
    Returns {name, title, detailed, innerclasses, sections, children} or None.
    `_seen` guards against the rare case of cycles in the group graph."""
    import xml.etree.ElementTree as _ET
    _seen = _seen if _seen is not None else set()
    if refid in _seen:
        return None
    _seen.add(refid)
    xml_path = xml_dir / f"{refid}.xml"
    if not xml_path.is_file():
        return None
    try:
        root = _ET.parse(xml_path).getroot()
    except _ET.ParseError:
        return None
    cd = root.find("compounddef")
    if cd is None:
        return None
    name = (cd.findtext("compoundname") or "").strip()
    title = (cd.findtext("title") or name).strip()
    # Detailed description (used on parent index pages; breathe handles it
    # on leaf pages, so we extract it for context-display only).
    detailed_el = cd.find("detaileddescription")
    detailed = _doxygen_desc_to_md(detailed_el, h_level=3)
    # Inner classes (public only). One read per class's XML for its brief.
    # `qualified` is what `{doxygenclass}` needs (e.g. cv::ocl::Context); the
    # innerclass element's text already carries that, but normalize spaces.
    innerclasses = []
    for ic in cd.findall("innerclass"):
        if ic.get("prot") != "public":
            continue
        ic_refid = ic.get("refid", "")
        qualified = " ".join((ic.text or "").split())
        innerclasses.append({
            "refid": ic_refid,
            "name": qualified,
            "qualified": qualified,
            "kind": "struct" if ic_refid.startswith("struct") else "class",
            "brief": _read_class_brief(ic_refid, xml_dir),
        })
    # Section members (typedefs, enums, functions, variables, macros).
    # `qualified` and `param_types` exist so we can emit per-member breathe
    # directives (doxygenenum / doxygenfunction / …) instead of one big
    # doxygengroup; the latter inlines every <innerclass> on the group page,
    # which is the opposite of Doxygen's group-page layout.
    sections: dict[str, list[dict]] = {}
    for sd in cd.findall("sectiondef"):
        for md in sd.findall("memberdef"):
            kind = md.get("kind", "")
            section_title = dict(_MEMBERDEF_SECTIONS).get(kind)
            if not section_title:
                continue
            qualified = (md.findtext("qualifiedname") or "").strip()
            if not qualified:
                qualified = (md.findtext("name") or "").strip()
            # Param types: <type> text plus any <array> suffix (Doxygen
            # stores `int foo[3]` as <type>int</type>...<array>[3]</array>;
            # without merging them our breathe disambiguator omits the `[3]`
            # and breathe reports "Unable to resolve function with
            # arguments (…, const double)" even though the function exists).
            def _param_type(p) -> str:
                t = _itertext(p.find("type"))
                arr = (p.findtext("array") or "").strip()
                return (t + arr) if arr else t
            param_types = [_param_type(p) for p in md.findall("param")]
            # Enum values + scoped-vs-unscoped flag (for Doxygen-style
            # synopsis rendering — one code block per enum with all values
            # inside `{ }`, instead of breathe's discrete signature blocks).
            enum_values = []
            is_strong = md.get("strong", "no") == "yes"
            if kind == "enum":
                for ev in md.findall("enumvalue"):
                    enum_values.append({
                        "name":        (ev.findtext("name") or "").strip(),
                        "initializer": (ev.findtext("initializer") or "").strip(),
                        "brief":       _itertext(ev.find("briefdescription")),
                    })
            sections.setdefault(section_title, []).append({
                "id":          md.get("id", ""),
                "kind":        kind,
                "name":        (md.findtext("name") or "").strip(),
                "qualified":   qualified,
                "type":        _itertext(md.find("type")),
                "args":        (md.findtext("argsstring") or "").strip(),
                "param_types": param_types,
                "brief":       _itertext(md.find("briefdescription")),
                "enum_values": enum_values,
                "strong":      is_strong,
            })
    # Recurse into subgroups.
    children = []
    for ig in cd.findall("innergroup"):
        child = _build_api_hierarchy(ig.get("refid"), xml_dir, _seen)
        if child is not None:
            children.append(child)
    return {"name": name, "title": title, "detailed": detailed,
            "innerclasses": innerclasses, "sections": sections,
            "children": children}


def _md_escape_cell(text: str) -> str:
    """Make `text` safe for a single Markdown table cell."""
    # Newlines collapse to spaces, pipes escape, angle brackets stay.
    return (text or "").replace("\n", " ").replace("\r", " ") \
                       .replace("|", "\\|").strip()


# Per-member breathe directive selector. The full doxygengroup directive
# recursively inlines every <innerclass> + <innernamespace>, which is the
# *opposite* of how Doxygen's group page lays out (classes are links to
# separate pages there). Emitting one directive per member keeps each
# member's detail block scoped to itself and lets us push classes to their
# own per-class pages — see _write_class_stub.
_MEMBER_DIRECTIVE = {
    "enum":     "doxygenenum",
    "function": "doxygenfunction",
    "typedef":  "doxygentypedef",
    "variable": "doxygenvariable",
    "define":   "doxygendefine",
}
# Section header for each member kind's detail block. Mirrors what Doxygen
# emits on a group page (e.g. group__core__opencl.html has separate
# "Enumeration Type Documentation" and "Function Documentation" sections,
# not one collapsed "Detailed Description").
_MEMBER_DETAIL_SECTION = {
    "Typedefs":     "Typedef Documentation",
    "Enumerations": "Enumeration Type Documentation",
    "Functions":    "Function Documentation",
    "Variables":    "Variable Documentation",
    "Macros":       "Macro Definition Documentation",
}


def _enum_synopsis_lines(m: dict) -> list[str]:
    """Render an enum as a Doxygen-style code synopsis: one `enum {…}` block
    listing every value with its initializer. Used in place of breathe's
    `{doxygenenum}` directive, which emits a discrete signature box per
    enumerator (one box for the enum + one per value) — that's the layout
    in the user's "before" screenshot. Doxygen's group page renders the
    enum as a single code-style box; this helper reproduces that.

    Value-name qualification follows Doxygen:
      * Scoped (`enum class`) → values prefixed with the enum's own
        qualified name.
      * Unscoped → values prefixed with the enum's *parent* scope
        (namespace or enclosing class), so they look like the C++ name
        you'd actually write in code."""
    qualified = m.get("qualified") or m["name"]
    is_strong = bool(m.get("strong"))
    keyword = "enum class" if is_strong else "enum"
    if is_strong:
        prefix = qualified + "::"
    elif "::" in qualified:
        prefix = qualified.rsplit("::", 1)[0] + "::"
    else:
        prefix = ""
    out = [f"{keyword} {qualified} {{"]
    vals = m.get("enum_values", []) or []
    for i, v in enumerate(vals):
        comma = "," if i < len(vals) - 1 else ""
        init = (" " + v["initializer"]) if v["initializer"] else ""
        out.append(f"    {prefix}{v['name']}{init}{comma}")
    out.append("}")
    return out


def _function_signature(member: dict) -> str:
    """Disambiguator used after a qualified function name in `{doxygenfunction}`.
    Breathe expects `name(type1, type2, …)` with parameter names dropped (it
    matches against Doxygen's `<param><type>` text). Empty-arg functions get
    `()` — required for breathe to match correctly even for non-overloads."""
    types = ", ".join((t or "").strip() for t in member.get("param_types", []))
    sig = f"({types})"
    if member.get("const"):
        sig += " const"
    return sig


def _class_page_name(refid: str) -> str:
    """Filename (without extension) for the per-class page. We use the Doxygen
    refid verbatim so MyST cross-refs and internal links from breathe stay
    stable (breathe's class anchors are the C++-mangled `_CPPv4N…` form, not
    the refid — so there's no collision with the page name)."""
    return refid


def _write_api_stub(node: dict, out_dir: pathlib.Path,
                    classes_seen: dict, ns_map: dict | None = None) -> None:
    """Write one .md per group node. Recurses into children.

    Parent groups (have <innergroup> children) → navigation index pages with
    @subpage toctrees. Leaf groups → Doxygen-style summary tables (Classes /
    Typedefs / Enumerations / Functions / Variables / Macros) at top, then
    a per-member detail block per kind (one breathe directive per member, not
    the recursive `{doxygengroup}` which inlines every nested class). Inner
    classes get their own pages — emitted later by `_generate_api_stubs` from
    `classes_seen`, which this fn populates."""
    name = node["name"]
    title = node["title"]
    out = out_dir / f"{name}.md"

    lines = [f"# {title} {{#api_{name}}}", ""]
    if node["children"]:
        lines += ["## Topics", ""]
        for child in node["children"]:
            lines.append(f"- @subpage api_{child['name']}")
        lines.append("")

    _has_content = bool(node["innerclasses"] or node["sections"] or node["children"])
    if node["detailed"]:
        lines += ["## Detailed Description", "", node["detailed"], ""]
    elif _has_content:
        lines += ["## Detailed Description", ""]

    if ns_map and ns_map.get(name):
        lines += ["## Namespaces", ""]
        for _ns_name, anchor in ns_map[name]:
            lines.append(f"- @subpage {anchor}")
        lines.append("")

    # Classes summary table — link to the per-class page that
    # `_generate_api_stubs` emits (one .md per refid, deduped across groups).
    if node["innerclasses"]:
        lines += ["## Classes", "",
                  "| Name | Description |", "|---|---|"]
        for c in node["innerclasses"]:
            classes_seen.setdefault(c["refid"], c)
            page = _class_page_name(c["refid"])
            link = f"[`{c['kind']} {c['name']}`]({page}.md)"
            lines.append(f"| {link} | {_md_escape_cell(c['brief'])} |")
        lines.append("")

    # Build a fast lookup of class qualified names known so far — used to
    # detect when a group's "Functions"/"Variables" sectiondef is actually
    # listing a class member (Doxygen groups span class boundaries). Such
    # members get rendered on the class page, not as standalone breathe
    # directives.
    class_qualifieds = {c.get("qualified") for c in classes_seen.values()
                        if c.get("qualified")}

    def _is_class_member(m: dict) -> bool:
        q = m.get("qualified") or ""
        if "::" not in q:
            return False
        parent = q.rsplit("::", 1)[0]
        return parent in class_qualifieds

    def _is_template_spec(m: dict) -> bool:
        # Explicit template specializations carry `<…>` in the name (Doxygen
        # stores `cv::saturate_cast< unsigned >`). breathe's C++ parser
        # rejects this as a function-name argument, so we can't emit a
        # `doxygenfunction` directive for them — the summary table still
        # lists them; only the per-member detail block is skipped.
        return "<" in (m.get("name") or "")


    # Section summary tables in Doxygen's order. For class-member items the
    # in-page anchor breathe would have emitted doesn't exist (we skip the
    # per-member directive below) — point the link at the parent class page
    # instead so the table stays clickable.
    def _member_anchor_link(m: dict, label: str) -> str:
        if _is_class_member(m):
            q = m["qualified"]
            parent_qualified = q.rsplit("::", 1)[0]
            for c in classes_seen.values():
                if c.get("qualified") == parent_qualified:
                    return f"[`{label}`]({_class_page_name(c['refid'])}.md)"
        return f"[`{label}`](#{m['id']})"

    for _, section_title in _MEMBERDEF_SECTIONS:
        items = node["sections"].get(section_title, [])
        if not items:
            continue
        lines.append(f"## {section_title}")
        lines.append("")
        if section_title == "Functions":
            lines += ["| Return | Name | Description |", "|---|---|---|"]
            for m in items:
                ret = _md_escape_cell(m["type"]) or "&nbsp;"
                label = f"{m['name']}{_md_escape_cell(m['args'])}"
                sig_link = _member_anchor_link(m, label)
                lines.append(
                    f"| `{ret}` | {sig_link} | {_md_escape_cell(m['brief'])} |")
        elif section_title in ("Typedefs", "Variables"):
            lines += ["| Type | Name | Description |", "|---|---|---|"]
            for m in items:
                t = _md_escape_cell(m["type"]) or "&nbsp;"
                name_link = _member_anchor_link(m, m["name"])
                lines.append(f"| `{t}` | {name_link} | {_md_escape_cell(m['brief'])} |")
        elif section_title == "Enumerations":
            # Code-style synopsis (Doxygen layout) instead of name/desc table.
            # Both summary and detail-block representations would duplicate
            # the same content — we only emit the synopsis here, and skip
            # enums in the detail-block loop below.
            for m in items:
                if m["brief"]:
                    lines.append(_md_escape_cell(m["brief"]))
                    lines.append("")
                lines.append("```cpp")
                lines.extend(_enum_synopsis_lines(m))
                lines.append("```")
                lines.append("")
            continue   # already appended trailing blank
        else:  # Macros
            lines += ["| Name | Description |", "|---|---|"]
            for m in items:
                name_link = _member_anchor_link(m, m["name"])
                lines.append(f"| {name_link} | {_md_escape_cell(m['brief'])} |")
        lines.append("")

    # Per-member detail blocks (Enumeration Type Documentation,
    # Function Documentation, …). One breathe directive per item — except
    # for enums, which are rendered as a single code-style synopsis (one
    # `enum {…}` block listing all values) to match Doxygen's group-page
    # layout. breathe's `{doxygenenum}` instead emits a discrete signature
    # box per enumerator, which looks fragmented and doesn't match the
    # reference rendering. Class members and template specializations are
    # skipped — see `_is_*`. Macros are deduped by name: Doxygen can emit
    # multiple <memberdef>s for an arity-overloaded macro, but breathe
    # renders the same C declaration each time and docutils complains
    # about duplicate IDs.
    seen_define_names: set[str] = set()
    for kind_key, section_title in _MEMBERDEF_SECTIONS:
        items = node["sections"].get(section_title, [])
        if not items:
            continue
        directive = _MEMBER_DIRECTIVE.get(kind_key)
        if not directive:
            continue
        # `rendered` collects entries with one of two shapes:
        #   ("breathe", spec, directive_name)  — emitted as a breathe block
        #   ("synopsis", brief, code_lines)    — emitted as a fenced block
        rendered = []
        for m in items:
            # Class members render on the class page; skip on the group page.
            if kind_key in ("function", "variable") and _is_class_member(m):
                continue
            # Explicit template specializations: breathe can't parse the
            # `<T>` in the function name, so we leave them out of the
            # detail section (table above still lists them).
            if _is_template_spec(m):
                continue
            if m["kind"] == "enum":
                # Enums are rendered as synopses in the "Enumerations"
                # summary section above; no separate detail block needed.
                continue
            qualified = m["qualified"] or m["name"]
            if m["kind"] == "function":
                # Always pass the param-types disambiguator. breathe sees
                # multiple matches for common names (e.g. cv::cos lives in
                # both core_quaternion and core_utils_softfloat) — without
                # a signature it can't pick. The XML patcher above
                # guarantees the matching <memberdef> is reachable for
                # breathe's lookup.
                spec = qualified + _function_signature(m)
            elif m["kind"] == "define":
                # Preprocessor macros aren't namespaced. Dedupe by name —
                # arity-overloaded macros (e.g. CV_LOG_VERBOSE) appear as
                # multiple memberdefs but render to the same C declaration.
                if m["name"] in seen_define_names:
                    continue
                seen_define_names.add(m["name"])
                spec = m["name"]
            else:
                spec = qualified
            rendered.append(("breathe", spec, directive))
        if not rendered:
            continue
        lines.append(f"## {_MEMBER_DETAIL_SECTION[section_title]}")
        lines.append("")
        for entry in rendered:
            if entry[0] == "synopsis":
                _, brief, code_lines = entry
                if brief:
                    lines.append(brief)
                    lines.append("")
                lines.append("```cpp")
                lines.extend(code_lines)
                lines.append("```")
                lines.append("")
            else:
                _, spec, dname = entry
                lines += [
                    f"```{{{dname}}} {spec}",
                    ":project: opencv",
                    "```",
                    "",
                ]

    # Hidden toctree for per-class pages — needed so Sphinx knows these
    # files exist and so the left sidebar lists them under this group.
    if node["innerclasses"]:
        lines += ["```{toctree}", ":hidden:", ":maxdepth: 1", ""]
        for c in node["innerclasses"]:
            lines.append(_class_page_name(c["refid"]))
        lines += ["```", ""]

    _stub_write(out, "\n".join(lines) + "\n")
    for child in node["children"]:
        _write_api_stub(child, out_dir, classes_seen, ns_map)


# Class-XML sectiondef kind → (summary heading, detail heading).
# Order in this mapping is the order Doxygen uses on a class page.
_CLASS_SUMMARY_SECTIONS = [
    ("public-type",             "Public Types"),
    ("public-func",             "Public Member Functions"),
    ("public-static-func",      "Static Public Member Functions"),
    ("public-attrib",           "Public Attributes"),
    ("public-static-attrib",    "Static Public Attributes"),
    ("protected-type",          "Protected Types"),
    ("protected-func",          "Protected Member Functions"),
    ("protected-static-func",   "Static Protected Member Functions"),
    ("protected-attrib",        "Protected Attributes"),
    ("protected-static-attrib", "Static Protected Attributes"),
    ("friend",                  "Friends"),
]


def _read_class_data(refid: str, xml_dir: pathlib.Path) -> dict | None:
    """Walk a class/struct compound XML and return everything the per-class
    page needs: brief + detailed for the class itself, and a list of
    members grouped by sectiondef kind. Returns None if the XML file is
    missing or unparseable — callers should fall back to a bare
    `{doxygenclass}` directive in that case.

    Each member dict carries the same fields `_build_api_hierarchy`
    captures, plus the `protection`, `static`, `const`, `virtual`,
    `explicit` flags from memberdef attributes (needed to render
    Doxygen-style annotations in the summary table)."""
    import xml.etree.ElementTree as _ET
    xml_path = xml_dir / f"{refid}.xml"
    if not xml_path.is_file():
        return None
    try:
        root = _ET.parse(xml_path).getroot()
    except _ET.ParseError:
        return None
    cd = root.find("compounddef")
    if cd is None:
        return None

    def _param_type(p) -> str:
        t = _itertext(p.find("type"))
        arr = (p.findtext("array") or "").strip()
        return (t + arr) if arr else t

    sections: dict[str, list[dict]] = {}
    for sd in cd.findall("sectiondef"):
        skind = sd.get("kind", "")
        items: list[dict] = []
        for md in sd.findall("memberdef"):
            mkind = md.get("kind", "")
            qualified = (md.findtext("qualifiedname") or "").strip()
            name = (md.findtext("name") or "").strip()
            enum_values = []
            if mkind == "enum":
                for ev in md.findall("enumvalue"):
                    enum_values.append({
                        "name":        (ev.findtext("name") or "").strip(),
                        "initializer": (ev.findtext("initializer") or "").strip(),
                        "brief":       _itertext(ev.find("briefdescription")),
                    })
            items.append({
                "id":          md.get("id", ""),
                "kind":        mkind,
                "name":        name,
                "qualified":   qualified or name,
                "type":        _itertext(md.find("type")),
                "args":        (md.findtext("argsstring") or "").strip(),
                "param_types": [_param_type(p) for p in md.findall("param")],
                "brief":       _itertext(md.find("briefdescription")),
                "static":      md.get("static") == "yes",
                "virt":        md.get("virt", "non-virtual"),
                "const":       md.get("const") == "yes",
                "explicit":    md.get("explicit") == "yes",
                "enum_values": enum_values,
                "strong":      md.get("strong", "no") == "yes",
            })
        if items:
            sections[skind] = items

    detailed_el = cd.find("detaileddescription")
    detailed_paras = []
    if detailed_el is not None:
        for p in detailed_el.findall("para"):
            txt = _itertext(p)
            if txt:
                detailed_paras.append(txt)

    return {
        "name":     (cd.findtext("compoundname") or "").strip(),
        "brief":    _itertext(cd.find("briefdescription")),
        "detailed": "\n\n".join(detailed_paras),
        "sections": sections,
    }


def _write_class_stub(cls: dict, out_dir: pathlib.Path,
                      xml_dir: pathlib.Path) -> None:
    """One .md per inner class. Mirrors Doxygen's class-page layout:
      * Brief + detailed description for the class itself
      * Summary tables, one per sectiondef kind (Public Member Functions,
        Static Public Member Functions, Protected Attributes, etc.)
      * Detail blocks per member, grouped Doxygen-style into Constructor &
        Destructor Documentation / Member Function Documentation /
        Member Data Documentation / etc.

    Detail blocks are per-member breathe directives (`{doxygenfunction}` /
    `{doxygenvariable}` / `{doxygentypedef}`), not the recursive
    `{doxygenclass} :members:` — the latter is breathe's discrete
    one-signature-per-method layout that the user wanted replaced.

    Falls back to a bare `{doxygenclass}` / `{doxygenstruct}` if the class
    XML can't be read (e.g. XML wasn't regenerated)."""
    page = _class_page_name(cls["refid"])
    out = out_dir / f"{page}.md"
    qualified = cls["qualified"] or cls["name"]
    kind_label = cls["kind"].title()  # "Class" / "Struct"
    title = f"{kind_label} {qualified}"
    # Note: no `{#refid}` anchor in the heading — duplicates the
    # docname-derived target. `_generate_api_stubs` seeds the
    # refid→docname mapping into `_ANCHOR_TO_DOC` for `@ref` resolution.
    lines = [f"# {title}", ""]

    data = _read_class_data(cls["refid"], xml_dir)
    if data is None:
        # Fallback for missing XML.
        directive = "doxygenstruct" if cls["kind"] == "struct" else "doxygenclass"
        lines += [
            f"```{{{directive}}} {qualified}",
            ":project: opencv",
            ":members:",
            ":protected-members:",
            ":undoc-members:",
            "```",
            "",
        ]
        # out.write_text("\n".join(lines), encoding="utf-8")
        _stub_write(out, "\n".join(lines))
        return

    # 1) Summary tables in Doxygen's order.
    for sd_kind, summary_title in _CLASS_SUMMARY_SECTIONS:
        items = data["sections"].get(sd_kind, [])
        if not items:
            continue
        lines.append(f"## {summary_title}")
        lines.append("")
        # Type-bearing sections (functions, variables, typedefs) get a
        # Return/Type column. Enum-bearing public-type sections get
        # rendered as code-block synopses instead (matches the group-page
        # treatment).
        non_enum_items = [m for m in items if m["kind"] != "enum"]
        enum_items = [m for m in items if m["kind"] == "enum"]
        if non_enum_items:
            lines += ["| Return | Name | Description |", "|---|---|---|"]
            for m in non_enum_items:
                ret = _md_escape_cell(m["type"]) or "&nbsp;"
                if m["static"]:
                    ret = "static " + ret
                sig = f"{m['name']}{_md_escape_cell(m['args'])}"
                sig_link = f"[`{sig}`](#{m['id']})"
                lines.append(
                    f"| `{ret}` | {sig_link} | {_md_escape_cell(m['brief'])} |")
            lines.append("")
        for m in enum_items:
            if m["brief"]:
                lines.append(_md_escape_cell(m["brief"]))
                lines.append("")
            lines.append("```cpp")
            lines.extend(_enum_synopsis_lines(m))
            lines.append("```")
            lines.append("")

    # 2) "Detailed Description" section for the class itself.
    if data["detailed"]:
        lines += ["## Detailed Description", "", data["detailed"], ""]

    # 3) Per-member detail blocks. Functions split into ctor/dtor vs
    #    others (mirrors Doxygen's "Constructor & Destructor Documentation"
    #    + "Member Function Documentation"). Variables → "Member Data
    #    Documentation". Typedefs → "Member Typedef Documentation".
    #    Enums → "Member Enumeration Documentation" (synopsis code block).
    class_simple = qualified.rsplit("::", 1)[-1]
    typedef_items: list[dict] = []
    enum_items_all: list[dict] = []
    ctor_dtor_items: list[dict] = []
    func_items: list[dict] = []
    var_items: list[dict] = []
    _detail_seen: set[str] = set()
    for sd_items in data["sections"].values():
        for m in sd_items:
            if m["id"] in _detail_seen:
                continue
            _detail_seen.add(m["id"])
            if m["kind"] == "typedef":
                typedef_items.append(m)
            elif m["kind"] == "enum":
                enum_items_all.append(m)
            elif m["kind"] == "function":
                if m["name"] == class_simple or m["name"] == f"~{class_simple}":
                    ctor_dtor_items.append(m)
                else:
                    func_items.append(m)
            elif m["kind"] == "variable":
                var_items.append(m)

    def _emit_member_directive(m: dict, directive: str, spec: str) -> list[str]:
        spec = spec.replace("< ", "<").replace(" >", ">")
        return [
            f"({m['id']})=",
            f"```{{{directive}}} {spec}",
            ":project: opencv",
            "```",
            "",
        ]

    if typedef_items:
        lines += ["## Member Typedef Documentation", ""]
        for m in typedef_items:
            lines += _emit_member_directive(m, "doxygentypedef", m["qualified"])

    if enum_items_all:
        # Enums render as code-block synopses (matches the group-page
        # treatment — breathe's `{doxygenenum}` gives the discrete
        # one-signature-per-value layout the user wanted replaced).
        lines += ["## Member Enumeration Documentation", ""]
        for m in enum_items_all:
            lines.append(f"({m['id']})=")
            if m["brief"]:
                lines.append(_md_escape_cell(m["brief"]))
                lines.append("")
            lines.append("```cpp")
            lines.extend(_enum_synopsis_lines(m))
            lines.append("```")
            lines.append("")

    # Dedupe functions: a method can appear in multiple sectiondefs (e.g.
    # the same memberdef appearing in `public-func` and again via a
    # `<member refid>` we inlined). The refid is unique per memberdef.
    def _dedupe(items: list[dict]) -> list[dict]:
        seen, out = set(), []
        for m in items:
            if m["id"] in seen:
                continue
            seen.add(m["id"])
            out.append(m)
        return out

    if ctor_dtor_items:
        lines += ["## Constructor & Destructor Documentation", ""]
        for m in _dedupe(ctor_dtor_items):
            spec = m["qualified"] + _function_signature(m)
            lines += _emit_member_directive(m, "doxygenfunction", spec)

    if func_items:
        lines += ["## Member Function Documentation", ""]
        for m in _dedupe(func_items):
            spec = m["qualified"] + _function_signature(m)
            lines += _emit_member_directive(m, "doxygenfunction", spec)

    if var_items:
        lines += ["## Member Data Documentation", ""]
        for m in _dedupe(var_items):
            lines += _emit_member_directive(m, "doxygenvariable", m["qualified"])

    # out.write_text("\n".join(lines), encoding="utf-8")
    _stub_write(out, "\n".join(lines))


def _patch_namespace_xml_for_breathe(xml_dir: pathlib.Path,
                                     out_dir: pathlib.Path) -> None:
    """Mirror `xml_dir` into `out_dir` via symlinks, then rewrite every
    *non-group* compound XML to inline `<memberdef>` blocks that exist only
    in the group XML.

    Why: Doxygen lists functions declared inside `@addtogroup` regions as
    `<member refid="group__…">` in the parent namespace XML (for `cv::*`
    free functions) or the parent file XML (for global `hal_ni_*`-style
    functions) — *without* a full `<memberdef>` block. The memberdef lives
    only in the group XML file. breathe's function-by-name lookup walks
    `<memberdef>` blocks in namespace/file XMLs and ignores bare refs, so
    directives like `{doxygenfunction} cv::log` or
    `{doxygenfunction} hal_ni_merge8u` fail with "Cannot find function".

    Patching: for each `<member refid>` in a target compound's sectiondef
    whose id targets `group__…`, we open the group XML, find the matching
    `<memberdef id="…">`, and append it into the compound's sectiondef.
    The original XML on disk is untouched.

    Freshness guard: skip the whole rebuild if `out_dir/index.xml` is at
    least as new as `xml_dir/index.xml`. The patcher parses ~1500 compound
    XMLs on a clean run — rerunning that on every sphinx-build is what
    made incremental rebuilds feel sluggish. `sphinx-xml` updates the
    source `xml/` tree's mtimes, so a real Doxygen change always
    invalidates this cache."""
    import xml.etree.ElementTree as _ET
    import os as _osmod, shutil as _shutil
    out_dir.mkdir(parents=True, exist_ok=True)

    # 1) Mirror every file from xml_dir into out_dir as a symlink. Cleaning
    #    out_dir each time keeps this idempotent across rebuilds (Doxygen
    #    XML changes are picked up because the symlinks resolve fresh).
    for child in list(out_dir.iterdir()):
        if child.is_symlink() or child.is_file():
            child.unlink()
        elif child.is_dir():
            _shutil.rmtree(child)
    for src in xml_dir.iterdir():
        dst = out_dir / src.name
        try:
            _osmod.symlink(src, dst)
        except (OSError, NotImplementedError):
            _shutil.copy2(src, dst)

    # 2) Cache for parsed group XMLs (each is read once even if referenced
    #    by many compounds).
    _group_cache: dict[str, _ET.ElementTree] = {}

    def _load_group(group_id: str):
        if group_id in _group_cache:
            return _group_cache[group_id]
        gx = xml_dir / f"{group_id}.xml"
        if not gx.is_file():
            _group_cache[group_id] = None
            return None
        try:
            _group_cache[group_id] = _ET.parse(gx)
        except _ET.ParseError:
            _group_cache[group_id] = None
        return _group_cache[group_id]

    # 3) For each non-group compound XML (namespace, file, dir, …) patch
    #    `<member refid>` entries that point at group memberdefs.
    #    `index.xml` is the project index (not a compound) → skip it.
    #    `class*.xml`/`struct*.xml`/`union*.xml` already carry full
    #    memberdefs for their methods, but they may *also* have
    #    `<member refid>` from @addtogroup tagged methods — patch them too.
    # Fast pre-filter: most compound XMLs don't reference any group at all;
    # parsing+walking them is wasted work. A bytes-level substring scan is
    # ~100x faster than ET.parse and lets us short-circuit those files.
    _SKIP_PREFIXES = ("group", "index")
    _GROUP_REF_MARKER = b'refid="group__'
    for compound_file in xml_dir.glob("*.xml"):
        if any(compound_file.name.startswith(p) for p in _SKIP_PREFIXES):
            continue
        try:
            raw = compound_file.read_bytes()
        except OSError:
            continue
        if _GROUP_REF_MARKER not in raw:
            continue  # no <member refid="group__…"> → nothing to patch
        try:
            tree = _ET.ElementTree(_ET.fromstring(raw))
        except _ET.ParseError:
            continue
        root = tree.getroot()
        cd = root.find("compounddef")
        if cd is None:
            continue
        existing_ids = {md.get("id") for md in cd.iter("memberdef")}
        patched = False
        for sd in cd.findall("sectiondef"):
            for member in list(sd.findall("member")):
                refid = member.get("refid", "")
                if not refid or refid in existing_ids:
                    continue
                # Member refids inside groups look like
                # "group__core__utils__softfloat_1ga…". The compound id is
                # everything before "_1" (which separates member from
                # compound in Doxygen's id scheme).
                if not refid.startswith("group__"):
                    continue
                sep = refid.find("_1")
                if sep < 0:
                    continue
                group_id = refid[:sep]
                gtree = _load_group(group_id)
                if gtree is None:
                    continue
                for md in gtree.getroot().iter("memberdef"):
                    if md.get("id") == refid:
                        sd.append(md)
                        existing_ids.add(refid)
                        patched = True
                        break
        if patched:
            out_file = out_dir / compound_file.name
            if out_file.is_symlink() or out_file.is_file():
                out_file.unlink()
            tree.write(out_file, encoding="utf-8", xml_declaration=True)


def _collect_all_nodes(node: dict) -> list[str]:
    return [node["name"]] + [n for c in node["children"] for n in _collect_all_nodes(c)]


def _build_ns_group_map(all_refids: list[str],
                        xml_dir: pathlib.Path) -> dict[str, set]:
    """Return namespace_name → set of group compound-names.
    Reads <innernamespace> directly from group XML (Doxygen 1.12+)."""
    import xml.etree.ElementTree as _ET
    ns_to_groups: dict[str, set] = {}
    for refid in all_refids:
        xml_path = xml_dir / f"{refid}.xml"
        if not xml_path.is_file():
            continue
        try:
            cd = _ET.parse(xml_path).getroot().find("compounddef")
        except _ET.ParseError:
            continue
        cname = (cd.findtext("compoundname") or "").strip()
        for inn in cd.findall("innernamespace"):
            ns_xml = xml_dir / f"{inn.get('refid', '')}.xml"
            if not ns_xml.is_file():
                continue
            try:
                ns_cd = _ET.parse(ns_xml).getroot().find("compounddef")
            except _ET.ParseError:
                continue
            if ns_cd is None:
                continue
            ns_name = (ns_cd.findtext("compoundname") or "").strip()
            if not (ns_cd.findall("sectiondef") or
                    ns_cd.findall("innerclass") or
                    ns_cd.findall("innernamespace")):
                continue
            ns_to_groups.setdefault(ns_name, set()).add(cname)
    return ns_to_groups


def _write_namespace_stub(ns: dict, out_dir: pathlib.Path,
                          xml_dir: pathlib.Path,
                          ns_group_map: dict | None = None,
                          group_info: dict | None = None) -> tuple[str, str]:
    """Write api/namespace_<slug>.md for one namespace. Returns (anchor, fname)."""
    import xml.etree.ElementTree as _ET
    slug = ns["name"].replace("::", "__")
    anchor = f"api_ns_{slug}"
    fname = f"namespace_{slug}.md"
    lines = [f"# {ns['name']} namespace {{#{anchor}}}", ""]
    if ns_group_map and group_info:
        crumbs: list[str] = []
        for grp in sorted(ns_group_map.get(ns["name"], set())):
            chain: list[str] = []
            cur: str | None = grp
            while cur and cur in group_info:
                chain.append(cur)
                cur = group_info[cur]["parent"]
            chain.reverse()
            parts = [f"[{group_info[g]['title']}]({g}.md)" for g in chain]
            if parts:
                crumbs.append(" » ".join(parts))
        if crumbs:
            lines += [" | ".join(crumbs), ""]
    if ns["brief"]:
        lines += [ns["brief"], ""]

    # Read member sections from patched XML (has inlined group memberdefs).
    ns_sections: dict[str, list[dict]] = {}
    ns_xml_path = _PATCHED_XML_DIR / f"{ns['refid']}.xml" if ns.get("refid") else None
    if ns_xml_path and ns_xml_path.is_file():
        try:
            cd_ns = _ET.parse(ns_xml_path).getroot().find("compounddef")
            if cd_ns is not None:
                for sd in cd_ns.findall("sectiondef"):
                    for md in sd.findall("memberdef"):
                        kind = md.get("kind", "")
                        section_title = dict(_MEMBERDEF_SECTIONS).get(kind)
                        if not section_title:
                            continue
                        qualified = (md.findtext("qualifiedname") or "").strip() or \
                                    (md.findtext("name") or "").strip()
                        # Skip class methods / sub-namespace members inlined via group patching.
                        _ns_pfx = ns["name"] + "::"
                        if qualified.startswith(_ns_pfx) and "::" in qualified[len(_ns_pfx):]:
                            continue
                        def _pt(p) -> str:
                            t = _itertext(p.find("type"))
                            arr = (p.findtext("array") or "").strip()
                            return (t + arr) if arr else t
                        enum_values = []
                        if kind == "enum":
                            for ev in md.findall("enumvalue"):
                                enum_values.append({
                                    "name":        (ev.findtext("name") or "").strip(),
                                    "initializer": (ev.findtext("initializer") or "").strip(),
                                    "brief":       _itertext(ev.find("briefdescription")),
                                })
                        ns_sections.setdefault(section_title, []).append({
                            "id":          md.get("id", ""),
                            "kind":        kind,
                            "name":        (md.findtext("name") or "").strip(),
                            "qualified":   qualified,
                            "type":        _itertext(md.find("type")),
                            "type_elem":   md.find("type"),
                            "static":      md.get("static") == "yes",
                            "args":        (md.findtext("argsstring") or "").strip(),
                            "param_types": [_pt(p) for p in md.findall("param")],
                            "brief":       _itertext(md.find("briefdescription")),
                            "enum_values": enum_values,
                            "strong":      md.get("strong", "no") == "yes",
                        })
        except _ET.ParseError:
            pass

    # Sub-namespaces: read <innernamespace> directly from the namespace XML.
    ns_prefix = ns["name"] + "::"
    innernamespaces = []
    if ns_xml_path and ns_xml_path.is_file():
        try:
            cd_ns2 = _ET.parse(ns_xml_path).getroot().find("compounddef")
            if cd_ns2 is not None:
                for inn in cd_ns2.findall("innernamespace"):
                    iname = (inn.text or "").strip()
                    irefid = inn.get("refid", "")
                    if iname:
                        innernamespaces.append((iname, irefid))
        except _ET.ParseError:
            pass
    def _ns_has_content(refid: str) -> bool:
        f = xml_dir / f"{refid}.xml"
        if not f.is_file():
            return True  # can't check → keep
        try:
            cd3 = _ET.parse(f).getroot().find("compounddef")
            return cd3 is not None and bool(
                cd3.findall("sectiondef") or
                cd3.findall("innerclass") or
                cd3.findall("innernamespace"))
        except _ET.ParseError:
            return True
    nonempty_ns = [(n, r) for n, r in innernamespaces if _ns_has_content(r)]
    if nonempty_ns:
        lines += ["## Namespaces", "", "| Namespace |", "|---|"]
        for iname, irefid in sorted(nonempty_ns, key=lambda x: x[0].lower()):
            short = iname[len(ns_prefix):] if iname.startswith(ns_prefix) else iname
            islug = iname.replace("::", "__")
            lines.append(f"| [namespace {short}](namespace_{islug}.md) |")
        lines.append("")

    # Namespace XML lacks <innerclass> in Doxygen 1.12; glob by refid prefix.
    refid_prefix = ns["name"].replace("::", "_1_1") + "_1_1"
    innerclasses = []
    for kind in ("struct", "class"):
        for xml_file in sorted(xml_dir.glob(f"{kind}{refid_prefix}*.xml")):
            try:
                cd2 = _ET.parse(xml_file).getroot().find("compounddef")
                if cd2 is None:
                    continue
                cname = (cd2.findtext("compoundname") or "").strip()
                if "::" in cname[len(ns_prefix):]:  # skip sub-namespace classes
                    continue
                brief = _itertext(cd2.find("briefdescription"))
                innerclasses.append((xml_file.stem, cname, kind, brief))
            except _ET.ParseError:
                continue
    if innerclasses:
        lines += ["## Classes", "", "| Name |", "|---|"]
        for ic_refid, ic_name, ic_kind, ic_brief in innerclasses:
            page = _class_page_name(ic_refid)
            short_name = ic_name[len(ns_prefix):]
            lines.append(f"| [`{ic_kind} {short_name}`]({page}.md) |")
        lines.append("")

    # Member summary tables.
    for kind_key, section_title in _MEMBERDEF_SECTIONS:
        items = ns_sections.get(section_title, [])
        if not items:
            continue
        lines.append(f"## {section_title}")
        lines.append("")
        if section_title == "Functions":
            lines += ["| Return | Name |", "|---|---|"]
            for m in items:
                ret_md = _type_to_md(m.get("type_elem"))
                if not ret_md:
                    ret_md = _md_escape_cell(m["type"]) or "&nbsp;"
                if m.get("static"):
                    ret_md = "static " + ret_md
                label = f"{m['name']}{_md_escape_cell(m['args'])}"
                lines.append(f"| {ret_md} | [`{label}`](#{m['id']}) |")
        elif section_title in ("Typedefs", "Variables"):
            for m in items:
                lines.append("```cpp")
                lines.append(f"typedef {m['type']} {m['name']}" if section_title == "Typedefs"
                              else f"{m['type']} {m['name']}")
                lines.append("```")
                lines.append("")
            continue
        elif section_title == "Enumerations":
            for m in items:
                if m["brief"]:
                    lines.append(_md_escape_cell(m["brief"]))
                    lines.append("")
                lines.append("```cpp")
                lines.extend(_enum_synopsis_lines(m))
                lines.append("```")
                lines.append("")
            continue
        else:  # Macros
            lines += ["| Name | Description |", "|---|---|"]
            for m in items:
                lines.append(f"| [`{m['name']}`](#{m['id']}) | {_md_escape_cell(m['brief'])} |")
        lines.append("")

    if not ns_sections and not innerclasses:
        lines += [
            "## Detailed Description",
            "",
            f"```{{doxygennamespace}} {ns['name']}",
            ":project: opencv",
            "```",
        ]
    elif ns.get("detailed"):
        lines += ["## Detailed Description", "", ns["detailed"], ""]

    # Per-member detail blocks.
    seen_define_names: set[str] = set()
    for kind_key, section_title in _MEMBERDEF_SECTIONS:
        items = ns_sections.get(section_title, [])
        if not items:
            continue

        # Enums: heading + declaration + Enumerator table with Python names.
        if section_title == "Enumerations":
            enum_items = [m for m in items if "<" not in (m.get("name") or "")]
            if enum_items:
                lines.append(f"## {_MEMBER_DETAIL_SECTION[section_title]}")
                lines.append("")
                for m in enum_items:
                    qualified = m["qualified"] or m["name"]
                    keyword = "enum class" if m.get("strong") else "enum"
                    lines.append(f"({m['id']})=")
                    lines.append(f"### {m['name']}")
                    lines.append("")
                    lines += [f"`{keyword} {qualified}`", ""]
                    if m.get("brief"):
                        lines += [_md_escape_cell(m["brief"]), ""]
                    vals = m.get("enum_values") or []
                    if vals:
                        has_desc = any(v.get("brief") for v in vals)
                        if has_desc:
                            lines += ["| Enumerator | Description |", "|---|---|"]
                        else:
                            lines += ["| Enumerator |", "|---|"]
                        for v in vals:
                            scope = qualified if m.get("strong") else ns["name"]
                            cpp_key = f"{scope}::{v['name']}"
                            py_entries = _PY_SIGNATURES.get(cpp_key, [])
                            py_name = py_entries[0]["name"] if py_entries else None
                            cell = f"`{v['name']}`"
                            if py_name:
                                cell += f"<br>Python: `{py_name}`"
                            if has_desc:
                                desc = _md_escape_cell(v.get("brief") or "")
                                lines.append(f"| {cell} | {desc} |")
                            else:
                                lines.append(f"| {cell} |")
                        lines.append("")
            continue

        directive = _MEMBER_DIRECTIVE.get(kind_key)
        if not directive:
            continue
        rendered = []
        for m in items:
            if "<" in (m.get("name") or ""):  # skip template specializations
                continue
            qualified = m["qualified"] or m["name"]
            if m["kind"] == "function":
                spec = qualified + _function_signature(m)
            elif m["kind"] == "define":
                if m["name"] in seen_define_names:
                    continue
                seen_define_names.add(m["name"])
                spec = m["name"]
            else:
                spec = qualified
            rendered.append((spec, directive))
        if not rendered:
            continue
        lines.append(f"## {_MEMBER_DETAIL_SECTION[section_title]}")
        lines.append("")
        for spec, dname in rendered:
            short = spec.split("(")[0].split("::")[-1]
            suffix = "()" if dname == "doxygenfunction" else ""
            lines += [
                f"### {short}{suffix}",
                "",
                f"```{{{dname}}} {spec}",
                ":project: opencv",
                "```",
                "",
            ]

    # (out_dir / fname).write_text("\n".join(lines) + "\n", encoding="utf-8")
    _stub_write(out_dir / fname, "\n".join(lines) + "\n")
    return anchor, fname


def _namespaces_for_group(group_name: str, xml_dir: pathlib.Path,
                          ns_group_map: dict[str, set]) -> list[dict]:
    """Return namespace dicts for a group from ns_group_map."""
    import xml.etree.ElementTree as _ET, glob as _glob
    wanted = {ns for ns, grps in ns_group_map.items() if group_name in grps}
    candidates: list[dict] = []
    for ns_name in sorted(wanted):
        ns_file = xml_dir / ("namespace" + "_1_1".join(ns_name.split("::")) + ".xml")
        if not ns_file.is_file():
            for f in _glob.glob(str(xml_dir / "namespacecv*.xml")):
                try:
                    cd = _ET.parse(f).getroot().find("compounddef")
                    if cd is not None and (cd.findtext("compoundname") or "").strip() == ns_name:
                        ns_file = pathlib.Path(f)
                        break
                except _ET.ParseError:
                    continue
        if not ns_file.is_file():
            continue
        try:
            cd = _ET.parse(ns_file).getroot().find("compounddef")
        except _ET.ParseError:
            continue
        if cd is None:
            continue
        brief = _itertext(cd.find("briefdescription"))
        detailed_el = cd.find("detaileddescription")
        detailed = "\n\n".join(
            p for p in (_itertext(e) for e in (detailed_el.findall("para") if detailed_el is not None else []))
            if p)
        candidates.append({"name": ns_name, "refid": cd.get("id", ""), "brief": brief, "detailed": detailed})
    return candidates


_stub_written: set[pathlib.Path] = set()  # populated by _stub_write during generation


def _stub_write(path: pathlib.Path, content: str) -> None:
    """Write only when content changed; track path for stale-file cleanup."""
    if not (path.is_file() and path.read_text(encoding="utf-8") == content):
        path.write_text(content, encoding="utf-8")
    _stub_written.add(path)


def _generate_api_stubs(modules, xml_dir, out_dir):
    """Two passes:
      1. Walk each module's group hierarchy → emit one group .md per node
         (parent index pages + leaf pages with summary tables + per-member
         detail blocks). `classes_seen` is populated as a side-effect.
      2. Emit one .md per unique inner class — these are the per-class
         subpages the group pages link to.

    Freshness guard: if `out_dir/api_root.markdown` is at least as new as
    `xml_dir/index.xml`, the existing stubs are valid — skip the
    multi-second hierarchy walk and just reseed `_ANCHOR_TO_DOC` from the
    already-written per-class stub filenames (class stubs don't carry a
    `{#refid}` heading anchor, so `_scan_internal` wouldn't recover them
    otherwise, and `@ref classcv_1_1Mat` in tutorials would fail to
    resolve)."""
    if not modules:
        return
    if not xml_dir.is_dir():
        return  # No XML yet (sphinx-xml not run); degrade silently.
    out_dir.mkdir(parents=True, exist_ok=True)
    global _stub_written
    _stub_written = set()
    root_lines = [
        "API Reference {#api_root}",
        "=============",
        "",
        "Sphinx-rendered API reference for OpenCV main modules. Each entry",
        "below is a module's umbrella `@defgroup`; sub-pages mirror the",
        "Doxygen subgroup hierarchy.",
        "",
    ]
    classes_seen: dict[str, dict] = {}
    for m in modules:
        tree = _build_api_hierarchy(
            "group__" + m.replace("_", "__"), xml_dir)
        if tree is None:
            continue
        root_lines.append(f"- @subpage api_{tree['name']}")
        all_nodes = _collect_all_nodes(tree)
        all_refids = ["group__" + n.replace("_", "__") for n in all_nodes]
        ns_group_map = _build_ns_group_map(all_refids, _API_XML_DIR)
        # Flatten the group tree into {name: {title, parent}} for breadcrumbs.
        group_info: dict[str, dict] = {}
        def _flatten(node: dict, parent: str | None) -> None:
            group_info[node["name"]] = {"title": node["title"], "parent": parent}
            for child in node.get("children", []):
                _flatten(child, node["name"])
        _flatten(tree, None)
        ns_map: dict[str, list] = {}
        for group_name in all_nodes:
            for ns in _namespaces_for_group(group_name, _API_XML_DIR, ns_group_map):
                anchor, _ = _write_namespace_stub(ns, out_dir, _API_XML_DIR,
                                                  ns_group_map, group_info)
                ns_map.setdefault(group_name, []).append((ns["name"], anchor))
        _write_api_stub(tree, out_dir, classes_seen, ns_map)
    # Per-class pages (one per unique refid across all groups). We also
    # seed `_ANCHOR_TO_DOC` directly with refid -> docname so `@ref`
    # cross-references in tutorial markdown (and in any group page) keep
    # working — the per-class page no longer carries a `{#refid}` heading
    # anchor (would duplicate the docname-derived target).
    for cls in classes_seen.values():
        _write_class_stub(cls, out_dir, xml_dir)
        _ANCHOR_TO_DOC[cls["refid"]] = f"api/{_class_page_name(cls['refid'])}"
    # (out_dir / "api_root.markdown").write_text("\n".join(root_lines) + "\n", encoding="utf-8")
    _stub_write(out_dir / "api_root.markdown", "\n".join(root_lines) + "\n")
    for _p in list(out_dir.iterdir()):
        if _p not in _stub_written:
            _p.unlink(missing_ok=True)


if API_MODULES:
    # 1) Build a patched XML tree breathe will read (inlines group-only
    #    <memberdef>s into namespace XML so name lookups succeed).
    if _API_XML_DIR.is_dir():
        _patch_namespace_xml_for_breathe(_API_XML_DIR, _PATCHED_XML_DIR)
    # 2) Generate the api/ stub tree from the ORIGINAL XML — the stub
    #    generator only reads group XML, which is unchanged.
    _generate_api_stubs(API_MODULES, _API_XML_DIR, SPHINX_INPUT_ROOT / "api")
    # Recursive scan picks up api_root.markdown + every group stub.
    _scan_internal(SPHINX_INPUT_ROOT / "api")

# External scan: every OTHER module's top-level table_of_content_*.markdown.
for _toc in (DOC_ROOT / "tutorials").glob("*/table_of_content_*.markdown"):
    if _toc.parent.name not in DOC_MODULES:
        _scan_external(_toc)

_scan_internal(DOC_ROOT / "js_tutorials" / "js_tutorials.markdown", base=DOC_ROOT)
for _m in DOC_JS_MODULES:
    _scan_internal(DOC_ROOT / "js_tutorials" / _m, base=DOC_ROOT)
for _toc in (DOC_ROOT / "js_tutorials").glob("*/js_table_of_contents_*.markdown"):
    if _toc.parent.name not in DOC_JS_MODULES:
        _scan_external(_toc)

_scan_internal(DOC_ROOT / "py_tutorials" / "py_tutorials.markdown", base=DOC_ROOT)
for _m in DOC_PY_MODULES:
    _scan_internal(DOC_ROOT / "py_tutorials" / _m, base=DOC_ROOT)
for _toc in (DOC_ROOT / "py_tutorials").glob("*/py_table_of_contents_*.markdown"):
    if _toc.parent.name not in DOC_PY_MODULES:
        _scan_external(_toc)

for _m in CONTRIB_MODULES:
    _tut_dir = CONTRIB_ROOT / _m / "tutorials"
    if not _tut_dir.is_dir():
        continue
    for _md in list(_tut_dir.rglob("*.markdown")) + list(_tut_dir.rglob("*.md")):
        try:
            _head = _md.read_text(encoding="utf-8", errors="replace")[:4000]
        except OSError:
            continue
        _rel = "tutorials_contrib/" + _m + "/" + _md.relative_to(_tut_dir).with_suffix("").as_posix()
        for _mm in re.finditer(r"\{#([\w-]+)\}", _head):
            _ANCHOR_TO_DOC[_mm.group(1)] = _rel

# Doxygen flattens IMAGE_PATH across every `images/` folder under the tutorial
# tree, so a tutorial can reference `images/foo.png` even when `foo.png` lives
# in a sibling module's `images/` directory. Mirror that behavior by building
# a basename -> doc-root-relative-path index once at import time.
_IMAGE_INDEX: dict[str, str] = {}
for _img_tree in (DOC_ROOT / "tutorials", DOC_ROOT / "js_tutorials", DOC_ROOT / "py_tutorials"):
    for _img in _img_tree.rglob("images/*"):
        if _img.is_file():
            _IMAGE_INDEX.setdefault(_img.name, _img.relative_to(DOC_ROOT).as_posix())
for _img in (DOC_ROOT / "js_tutorials" / "js_assets").glob("*"):
    if _img.is_file():
        _IMAGE_INDEX.setdefault(_img.name, _img.relative_to(DOC_ROOT).as_posix())
for _img in (DOC_ROOT / "images").glob("*"):
    if _img.is_file():
        _IMAGE_INDEX.setdefault(_img.name, _img.relative_to(DOC_ROOT).as_posix())
_IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".bmp", ".webp"}
for _m in CONTRIB_MODULES:
    _tut_dir = CONTRIB_ROOT / _m / "tutorials"
    if _tut_dir.is_dir():
        for _img in _tut_dir.rglob("*"):
            if _img.is_file() and _img.suffix.lower() in _IMAGE_EXTS:
                _rel_str = "tutorials_contrib/" + _m + "/" + _img.relative_to(_tut_dir).as_posix()
                _IMAGE_INDEX.setdefault(_img.name, _rel_str)
    # Contrib images outside <m>/tutorials/. URL is contrib_modules/<m>/<rest>.
    # Files served via html_extra_path — no copies in srcdir.
    for _sub in ("doc", "samples"):
        _src = CONTRIB_ROOT / _m / _sub
        if _src.is_dir():
            for _img in _src.rglob("*"):
                if _img.is_file() and _img.suffix.lower() in _IMAGE_EXTS:
                    _rel = _img.relative_to(CONTRIB_ROOT).as_posix()
                    _IMAGE_INDEX.setdefault(_img.name, f"contrib_modules/{_rel}")

_TOGGLE_LABELS = {"cpp": "C++", "java": "Java", "python": "Python"}


# Mirror of Doxygen's EXAMPLE_PATH (see opencv/doc/Doxyfile.in) — the bases a
# bare `@snippet some/path.cpp` is resolved against. OPENCV_ROOT comes first so
# fully-qualified paths like `samples/cpp/...` keep working.
_SNIPPET_BASES = [
    OPENCV_ROOT,
    OPENCV_ROOT / "samples",
    OPENCV_ROOT / "apps",
    CONTRIB_ROOT,
]


def _read_snippet(rel_path: str, label: str | None) -> tuple[str, str]:
    """Return (code_text, language) for an @include / @snippet directive."""
    p = next((b / rel_path for b in _SNIPPET_BASES
              if (b / rel_path).is_file()), None)
    if p is None:
        return f"// not found: {rel_path}\n", "text"
    text = p.read_text(encoding="utf-8", errors="replace")
    ext = p.suffix.lower()
    lang = {".cpp": "cpp", ".hpp": "cpp", ".h": "cpp", ".c": "c",
            ".py": "python", ".java": "java"}.get(ext, "text")
    if label is None:
        return text, lang
    # Doxygen matches `[label]` after any comment-style marker (//, //!, #, ##)
    # anywhere on a line — including labels wrapped in block-comments like
    # `/* //! [label]` or `//! [label] */`.
    pat = re.compile(r"^[^\[\n]*(?://!|//|##|#)[^\[\n]*\[" + re.escape(label)
                     + r"\][^\n]*$", re.MULTILINE)
    matches = list(pat.finditer(text))
    if len(matches) < 2:
        return f"// snippet not found: {rel_path} [{label}]\n", lang
    body = text[matches[0].end():matches[1].start()].strip("\n")
    lines = body.split("\n")
    indents = [len(l) - len(l.lstrip(" ")) for l in lines if l.strip()]
    if indents:
        dedent = min(indents)
        lines = [l[dedent:] if len(l) >= dedent else l for l in lines]
    return "\n".join(lines), lang


def _emit_toggles(tabs: list[tuple[str, str]], indent: str = "") -> str:
    def _dedent(body: str) -> str:
        # Fully dedent the tab-item body to column 0. The {tab-item} directive
        # resets the parser context, so directives (```{code-block}```) must be
        # at column 0 to be recognised — not at 4-space where CommonMark treats
        # them as indented code blocks. Using the actual minimum indent of all
        # non-empty lines (instead of exactly len(indent)) handles the case
        # where @snippet/@include is indented deeper than @add_toggle (e.g.
        # 8-space snippet inside a 4-space toggle block).
        lines = body.split("\n")
        non_empty = [l for l in lines if l.strip()]
        if not non_empty:
            return body
        min_ind = min(len(l) - len(l.lstrip()) for l in non_empty)
        if min_ind == 0:
            return body
        return "\n".join(l[min_ind:] if l.strip() else l for l in lines)
    if HAVE_SPHINX_DESIGN:
        out = ["", "``````{tab-set}"]
        for lang, body in tabs:
            label = _TOGGLE_LABELS.get(lang, lang.title())
            out += [f"`````{{tab-item}} {label}", _dedent(body), "`````"]
        out += ["``````", ""]
        return "\n".join(out)
    # Fallback: render each toggle as a labeled section.
    out = [""]
    for lang, body in tabs:
        label = _TOGGLE_LABELS.get(lang, lang.title())
        out += [f"**{label}**", "", _dedent(body), ""]
    return "\n".join(out)


def _translate(text: str, docname: str | None = None) -> str:
    # 0. \htmlonly → raw HTML; rewrite relative iframe src to absolute URL.
    def _htmlonly_repl(m: re.Match) -> str:
        body = re.sub(r'src="\.\.\/\.\.\/([\w/.-]+)"',
                      lambda mm: f'src="{DOXYGEN_BASE_URL}{mm.group(1)}"',
                      m.group(1))
        body = re.sub(r'\s*onload="[^"]*"', ' height="700px"', body)
        return f"\n```{{raw}} html\n{body}\n```\n"
    text = re.sub(r"^\\htmlonly\s*\n(.*?)\\endhtmlonly\s*$",
                  _htmlonly_repl, text, flags=re.DOTALL | re.MULTILINE)

    # 1. Heading anchors: "Title {#name}\n===" (setext) and "## Title {#name}" (ATX).
    #    Strip the anchor from the rendered heading and emit a MyST label
    #    "(name)=" immediately above. Setext converted to ATX for simplicity.
    def _setext_repl(m: re.Match) -> str:
        title = m.group("title").strip()
        level = 1 if m.group("bar") == "=" else 2
        return f"({m.group('anchor')})=\n{'#' * level} {title}"
    text = re.sub(
        r"^(?P<title>[^\n]+?)\s*\{#(?P<anchor>[\w-]+)\}\s*\n(?P<bar>[=\-])[=\-]{2,}\s*$",
        _setext_repl, text, flags=re.MULTILINE)
    text = re.sub(
        r"^(?P<hashes>#+)\s+(?P<title>[^\n]+?)\s*\{#(?P<anchor>[\w-]+)\}\s*$",
        lambda m: f"({m.group('anchor')})=\n{m.group('hashes')} {m.group('title')}",
        text, flags=re.MULTILINE)
    text = re.sub(
        r"^(?P<title>[^\n{]+?)\s*\n={3,}\s*$",
        lambda m: f"## {m.group('title').strip()}",
        text, flags=re.MULTILINE)
    # 1c. "N. Title\n---" Setext h2 — CommonMark parses "N. " as a list item, not a heading.
    text = re.sub(
        r"^(\d+\.[^\n]+)\n-{3,}\s*$",
        lambda m: f"## {m.group(1).strip()}",
        text, flags=re.MULTILINE)

    # 1a-i. Over-indented list markers.
    #   A bullet marker (-/*/+) followed by 5+ spaces causes CommonMark to use
    #   marker_col+2 as the continuation indent (the extra spaces beyond 4 are
    #   treated as content), making the actual text 6+ spaces into the content —
    #   above the 4-space threshold for indented code blocks.  Normalise to
    #   exactly 3 spaces after the marker and reduce all continuation lines by
    #   the same delta so nested structure is preserved.
    def _normalize_over_indented_markers(src: str) -> str:
        lines_in = src.split("\n")
        out: list[str] = []
        i = 0
        while i < len(lines_in):
            m = re.match(r"^([ \t]*)([-*+])( {5,})(.*)", lines_in[i])
            if m:
                outer, marker, spaces, content = (
                    m.group(1), m.group(2), m.group(3), m.group(4))
                old_col = len(outer) + 1 + len(spaces)
                new_col = len(outer) + 1 + 3
                delta = old_col - new_col
                out.append(f"{outer}{marker}   {content}")
                i += 1
                while i < len(lines_in):
                    line = lines_in[i]
                    stripped = line.lstrip(" \t")
                    actual = len(line) - len(stripped)
                    if not stripped:
                        out.append(line); i += 1; continue
                    if actual >= old_col:
                        out.append(" " * (actual - delta) + stripped); i += 1
                    else:
                        break
            else:
                out.append(lines_in[i]); i += 1
        return "\n".join(out)
    text = _normalize_over_indented_markers(text)

    # 1a-ii. 4-space indented list items directly under a heading.
    #   Doxygen ignores visual indentation; CommonMark treats 4-space-indented
    #   lines as indented code blocks.  Strip the 4-space prefix when list items
    #   immediately follow a heading (with optional blank lines between).
    text = re.sub(
        r"(^#{1,6}[ \t][^\n]+\n(?:[ \t]*\n)*)((?:    [ \t]*[-*+][^\n]*\n)+)",
        lambda m: m.group(1) + re.sub(r"^    ", "", m.group(2), flags=re.MULTILINE),
        text, flags=re.MULTILINE)

    # 1a-iii. 4-space list items under a plain paragraph line → strip to fix lazy continuation.
    text = re.sub(
        r"(^(?![ \t#@`]|-#|[-*+]\s|\d+[.)]\s)[^\n]+\n)((?:    [-*+][ \t][^\n]*\n(?:[ \t]{5,}[^\n]*\n)*)+)",
        lambda m: m.group(1) + re.sub(r"^    ", "", m.group(2), flags=re.MULTILINE),
        text, flags=re.MULTILINE)

    # 1b0. \b word → **word** (Doxygen bold macro — single next word only).
    text = re.sub(r"\\b\s+(\S+)", r"**\1**", text)

    # 1b. @note ... / @see ...  -> MyST admonitions.
    #     Runs BEFORE math conversion so that \f[...\f] inside a note body is
    #     still on one logical line and does not create a blank-line terminator
    #     that would cut the body short.
    #     Allow optional leading indent and bare @note (body on next line).
    #     Dedent the body so indented lines don't become code blocks inside
    #     the directive.
    _ADMON_KIND = {"note": "note", "see": "seealso", "warning": "warning", "sa": "seealso"}
    def _admon_repl(m: re.Match) -> str:
        indent = m.group("indent")
        kind = _ADMON_KIND[m.group("dir")]
        raw = m.group("body")
        lines = raw.split("\n")
        min_ind = min(
            (len(l) - len(l.lstrip()) for l in lines if l.strip()), default=0)
        body = "\n".join(l[min_ind:] for l in lines).strip()
        return f"\n{indent}:::{{{kind}}}\n{indent}{body}\n{indent}:::\n"
    text = re.sub(
        r"^(?P<indent>[ \t]*)@(?P<dir>note|see|warning|sa)[ \t]*\n?(?P<body>.+?)(?=\n[ \t]*\n|\n[ \t]*@[A-Za-z]|\Z)",
        _admon_repl, text, flags=re.DOTALL | re.MULTILINE)

    # 1c. @param / @return → MyST definition list.
    def _param_block_repl(m: re.Match) -> str:
        items: list[list] = []
        for line in m.group(0).split("\n"):
            pm = re.match(r"^@param\s+(\S+)\s*(.*)", line)
            rm = re.match(r"^@return\s*(.*)", line)
            if pm:
                items.append([f"`{pm.group(1)}`", [pm.group(2).strip()]])
            elif rm:
                items.append(["*(return value)*", [rm.group(1).strip()]])
            elif items and line.strip():
                items[-1][1].append(line.strip())
        def _inline_block_math(s: str) -> str:
            if re.match(r"^\\f\[.+\\f\]$", s.strip()):
                return s
            return re.sub(r"\\f\[(.+?)\\f\]", lambda mm: f"${mm.group(1).strip()}$", s)
        result = []
        has_param = False
        for key, desc_lines in items:
            desc_lines = [l for l in desc_lines if l]
            if not desc_lines:
                continue
            if key != "*(return value)*":
                has_param = True
            entry = f"{key}\n: {_inline_block_math(desc_lines[0])}"
            for cont in desc_lines[1:]:
                entry += f"\n  {_inline_block_math(cont)}"
            result.append(entry)
        header = "\n**Parameters**\n\n" if has_param else "\n"
        return header + "\n\n".join(result) + "\n"
    text = re.sub(
        r"((?:^@(?:param\s+\S+|return)\s+[^\n]+\n(?:[ \t]+[^\n]+\n)*)+)",
        _param_block_repl, text, flags=re.MULTILINE)

    # 2. Doxygen LaTeX math markers.
    #    Block \f[...\f]: consume leading indent and re-emit it on the $$
    #    fence lines so the block stays inside any enclosing list item and
    #    the text that follows (at the same indent) is not misread as a code block.
    #
    #    Preprocess: when two \f[...\f] blocks are adjacent on the same source
    #    line (e.g. \end{bmatrix}\f]\f[G_{y}), split them onto separate lines
    #    and prefix the second \f[ with the line's own leading indent.  Without
    #    this the primary regex below would convert the first block correctly but
    #    leave the second \f[ at column 0 in the output; the fallback then emits
    #    that block at column 0, breaking any enclosing list structure.
    def _split_adj_math(m: re.Match) -> str:
        indent = m.group("indent")
        return m.group(0).replace("\\f]\\f[", f"\\f]\n{indent}\\f[")
    text = re.sub(r"^(?P<indent>[ \t]*)[^\n]*\\f\]\\f\[",
                  _split_adj_math, text, flags=re.MULTILINE)

    def _block_math_repl(m: re.Match) -> str:
        ind = m.group("indent")
        return f"\n{ind}$$\n{m.group('body').strip()}\n{ind}$$\n"
    text = re.sub(r"^(?P<indent>[ \t]*)\\f\[(?P<body>.+?)\\f\]",
                  _block_math_repl, text, flags=re.DOTALL | re.MULTILINE)
    # Fallback for any \f[...\f] not at line-start (e.g. two adjacent blocks).
    text = re.sub(r"\\f\[(.+?)\\f\]",
                  lambda m: f"\n$$\n{m.group(1).strip()}\n$$\n",
                  text, flags=re.DOTALL)
    # Inline math.
    text = re.sub(r"\\f\$(.+?)\\f\$", lambda m: f"${m.group(1)}$",
                  text, flags=re.DOTALL)

    # 2b. Normalise unknown Pygments lexer names: plaintext/bash/sh → text.
    text = re.sub(r"^([ \t]*)```plaintext\b", r"\1```text", text, flags=re.MULTILINE)
    text = re.sub(r"^([ \t]*)```(?:bash|sh)\b", r"\1```text", text, flags=re.MULTILINE)

    # 3. @code{.lang} ... @endcode
    # Capture leading indent so the fence stays inside any enclosing list item.
    def _code_repl(m: re.Match) -> str:
        indent = m.group("indent")
        lang = (m.group("lang") or "").strip(".") or "text"
        if lang in ("none", "plaintext"):
            lang = "text"
        if lang in ("bash", "sh"):
            lang = "text"
        if lang == "m":
            lang = "objc"
        if lang == "js":
            lang = "javascript"
        raw = m.group("body").split("\n")
        non_empty = [l for l in raw if l.strip()]
        min_ind = min((len(l) - len(l.lstrip()) for l in non_empty), default=0)
        lines = [l[min_ind:] if l.strip() else "" for l in raw]
        while lines and not lines[0].strip():
            lines.pop(0)
        while lines and not lines[-1].strip():
            lines.pop()
        body = "\n".join(indent + l for l in lines)
        return f"\n{indent}```{{code-block}} {lang}\n{body}\n{indent}```\n"
    text = re.sub(
        r"^(?P<indent>[ \t]*)@code(?:\{(?P<lang>[^}]*)\})?\s*\n(?P<body>.*?)\n?[ \t]*@endcode",
        _code_repl, text, flags=re.DOTALL | re.MULTILINE)

    # 4. @include path  /  @includelineno path
    #    Indent preserved so code blocks inside list items don't break the list.
    def _include_repl(m: re.Match) -> str:
        indent = m.group("indent")
        code, lang = _read_snippet(m.group("path"), None)
        body = "\n".join(indent + l for l in code.rstrip().split("\n"))
        return f"\n{indent}```{{code-block}} {lang}\n{body}\n{indent}```\n"
    text = re.sub(r"^(?P<indent>[ \t]*)@include(?:lineno)?\s+(?P<path>\S+)",
                  _include_repl, text, flags=re.MULTILINE)

    # 4b. Remove stray @snippet that immediately follows @end_toggle at the same
    #     indent (no blank line between them). These are Doxygen fallback snippets
    #     for non-toggle Doxygen mode; the Sphinx build already shows them inside
    #     the tab-set, so the stray duplicate must be dropped before step 5
    #     converts it to a {code-block} that would land at document level with an
    #     invalid 4-space closing fence.
    text = re.sub(
        r"(^([ \t]*)@end_toggle[ \t]*\n)\2@snippet[^\n]*\n",
        r"\1",
        text, flags=re.MULTILINE)

    # 5. @snippet path [Label]
    # Indent preserved so code blocks inside list items don't break the list.
    def _snippet_repl(m: re.Match) -> str:
        indent = m.group("indent")
        code, lang = _read_snippet(m.group("path"), m.group("label"))
        body = "\n".join(indent + l for l in code.rstrip().split("\n"))
        return f"\n{indent}```{{code-block}} {lang}\n{body}\n{indent}```\n"
    text = re.sub(r"^(?P<indent>[ \t]*)@snippet\s+(?P<path>\S+)\s+(?P<label>[^\n]+?)\s*$",
                  _snippet_repl, text, flags=re.MULTILINE)

    # 5b. @snippetlineno — same as @snippet with :linenos:.
    def _snippetlineno_repl(m: re.Match) -> str:
        indent = m.group("indent")
        code, lang = _read_snippet(m.group("path"), m.group("label"))
        body = "\n".join(indent + l for l in code.rstrip().split("\n"))
        return f"\n{indent}```{{code-block}} {lang}\n{indent}:linenos:\n{body}\n{indent}```\n"
    text = re.sub(r"^(?P<indent>[ \t]*)@snippetlineno\s+(?P<path>\S+)\s+(?P<label>[^\n]+?)\s*$",
                  _snippetlineno_repl, text, flags=re.MULTILINE)

    # 6. @add_toggle_LANG ... @end_toggle  (coalesce runs into one tab-set)
    #    Capture the leading indent of each toggle block and emit the tab-set
    #    fence lines at the same indent, so toggles inside list items stay as
    #    list-item continuation content (where 4-space fences are valid).
    #    Body content is dedented so code blocks at column 0 inside the
    #    directive body are parsed correctly by myst-parser.
    def _toggle_collapse(src: str) -> str:
        out, i = [], 0
        opener = re.compile(r"^([ \t]*)@add_toggle_(\w+)[ \t]*$", re.MULTILINE)
        while True:
            m = opener.search(src, i)
            if not m:
                out.append(src[i:]); break
            out.append(src[i:m.start()])
            block_ind, tabs, j = m.group(1), [], m.start()
            while True:
                m2 = re.match(
                    r"[ \t]*@add_toggle_(\w+)[ \t]*\n(.*?)\n[ \t]*@end_toggle[ \t]*\n?",
                    src[j:], flags=re.DOTALL)
                if not m2:
                    break
                tabs.append((m2.group(1), m2.group(2)))
                j += m2.end()
                k = re.match(r"\s*", src[j:])
                if not k or not re.match(r"@add_toggle_", src[j + k.end():]):
                    break
                j += k.end()
            if not tabs:
                out.append(src[m.start():m.start() + 1]); i = m.start() + 1; continue
            out.append(_emit_toggles(tabs, block_ind))
            i = j
        return "".join(out)
    text = _toggle_collapse(text)

    # 6b. Strip list-item continuation indent stranded after a col-0 tab-set close.
    def _strip_tabset_continuations(src: str) -> str:
        lines = src.split("\n")
        out: list[str] = []
        i = 0
        while i < len(lines):
            line = lines[i]
            if line == "``````":
                out.append(line)
                i += 1
                while i < len(lines):
                    ln = lines[i]
                    if ln.startswith("    "):
                        out.append(ln[4:])
                        i += 1
                    elif not ln.strip():
                        out.append(ln)
                        i += 1
                    else:
                        break
            else:
                out.append(line)
                i += 1
        return "\n".join(out)
    text = _strip_tabset_continuations(text)

    # 7. toctree before @ref so list-item @ref tutorial_* entries aren't converted first
    def _subpage_list_to_toctree(src: str) -> str:
        if not src.endswith("\n"):
            src += "\n"
        pat = re.compile(
            r"((?:^[ \t]*-\s+@(?:subpage\s+[\w-]+|ref\s+tutorial_[\w-]+)[^\n]*\n(?:(?:[ \t]*\n)?[ \t]+[^\n]+\n)*)+)",
            re.MULTILINE)
        def repl(m: re.Match) -> str:
            block = m.group(1)
            entries = re.findall(r"@(?:subpage|ref)\s+([\w-]+)", block)
            dm = re.search(
                r"@(?:subpage|ref)\s+[\w-]+[^\n]*\n((?:(?:[ \t]*\n)?[ \t]+[^\n]+\n)*)", block)
            desc_raw = dm.group(1) if dm and dm.group(1) else ""
            _groups: list[list[str]] = [[]]
            for _l in desc_raw.splitlines():
                if _l.strip():
                    _groups[-1].append(_l.strip())
                elif _groups[-1]:
                    _groups.append([])
            desc = "\n\n".join(" ".join(g) for g in _groups if g) or None
            lines = []
            for e in entries:
                if e in _ANCHOR_TO_DOC:
                    lines.append("/" + _ANCHOR_TO_DOC[e])
                elif e in _ANCHOR_TO_EXTERNAL:
                    title, url = _ANCHOR_TO_EXTERNAL[e]
                    lines.append(f"{title} <{url}>")
                elif e in _TAG_FILENAMES:
                    title = _TAG_PAGE_TITLES.get(e, e)
                    lines.append(f"{title} <{DOXYGEN_BASE_URL + _TAG_FILENAMES[e]}>")
            if not lines:
                return ""
            body = "\n".join(lines)
            result = f"\n```{{toctree}}\n:maxdepth: 1\n\n{body}\n```\n"
            if desc:
                result += f"\n{desc}\n"
            return result
        return pat.sub(repl, src)
    text = _subpage_list_to_toctree(text)

    # 7b. @ref name [optional "Display Text"]
    def _ref_repl(m: re.Match) -> str:
        name = m.group("name"); disp = m.group("disp")
        target = _ANCHOR_TO_DOC.get(name)
        if target:
            return f"[{disp or name}]({'/' + target})"
        return f"[{disp or name}](#{name})"
    text = re.sub(r'@ref\s+(?P<name>[\w:-]+)(?:\s+"(?P<disp>[^"]+)")?',
                  _ref_repl, text)

    # 7b. cv.Name → [cv.Name](doxygen url) for names in the API index.
    if _CV_API:
        def _cvlink_repl(m: re.Match) -> str:
            url = _CV_API.get(m.group(1))
            return f'[cv.{m.group(1)}]({url})' if url else m.group(0)
        _parts = re.split(r'(```.*?```|`[^`\n]+`)', text, flags=re.DOTALL)
        text = ''.join(
            p if i % 2 else re.sub(
                r'(?<!\[)(?<!\()cv\.([A-Za-z][A-Za-z0-9_]*)',
                _cvlink_repl, p)
            for i, p in enumerate(_parts))

    # 8. @cite KEY → [[KEY]](link to docs.opencv.org citelist)
    text = re.sub(
        r"@cite\s+([\w-]+)",
        lambda m: f"[[{m.group(1)}]](https://docs.opencv.org/5.x/d0/de3/citelist.html#CITEREF_{m.group(1)})",
        text)

    # 8b. @youtube{ID}  -> responsive embed (raw HTML, passed through by MyST).
    text = re.sub(
        r"^@youtube\{(?P<id>[\w-]+)\}\s*$",
        lambda m: (
            '\n<div class="opencv-youtube">'
            f'<iframe src="https://www.youtube-nocookie.com/embed/{m.group("id")}" '
            'title="YouTube video player" frameborder="0" '
            'allow="accelerometer; autoplay; clipboard-write; encrypted-media; '
            'gyroscope; picture-in-picture" allowfullscreen></iframe></div>\n'
        ),
        text, flags=re.MULTILINE)

    # 9. @next_tutorial / @prev_tutorial  -> drop
    text = re.sub(r"^@(?:next|prev)_tutorial\{[^}]*\}\s*$", "",
                  text, flags=re.MULTILINE)

    # 10a. col-8 -# under a col-0 parent: move to col 5 (avoids CommonMark code-block threshold).
    def _fix_nested_ordered(src: str) -> str:
        lines = src.split("\n")
        out = []
        for i, line in enumerate(lines):
            if re.match(r"^        -#\s", line):
                nearest = ""
                for j in range(i - 1, max(i - 40, -1), -1):
                    m = re.match(r"^([ \t]*)-", lines[j])
                    if m and len(m.group(1)) < 8:
                        nearest = lines[j]
                        break
                out.append(line if (nearest and re.match(r"^    ", nearest)) else "     " + line[8:])
            else:
                out.append(line)
        return "\n".join(out)
    text = _fix_nested_ordered(text)

    # 10b. Doxygen ordered-list marker: "-#" -> "1."
    text = re.sub(r"^([ \t]*)-#([ \t]+)", r"\g<1>1.\g<2>",
                  text, flags=re.MULTILINE)

    # 11. @tableofcontents -> drop (PyData right sidebar replaces it)
    text = re.sub(r"^@tableofcontents\s*$", "", text, flags=re.MULTILINE)

    # 11b. @cond NAME ... @endcond  -> strip just the markers; if the
    #      enclosed @subpage points to a disabled module it gets dropped
    #      by _subpage_list_to_toctree above.
    text = re.sub(r"^@cond\s+\S+\s*$", "", text, flags=re.MULTILINE)
    text = re.sub(r"^@endcond\s*$", "", text, flags=re.MULTILINE)

    text = re.sub(r"</?center>", "", text, flags=re.IGNORECASE)

    # 11d. Escape C++ template <Type> in paragraph text; skip fenced code blocks.
    _cpp_tpl_re = re.compile(r'\b(\w+)<([A-Za-z_][\w:, *&]*?)>')
    _lines_out: list[str] = []
    _in_fence = False
    for _ln in text.splitlines(keepends=True):
        if re.match(r'^\s*```', _ln):
            _in_fence = not _in_fence
        if not _in_fence:
            _ln = _cpp_tpl_re.sub(lambda m: f'{m.group(1)}&lt;{m.group(2)}&gt;', _ln)
        _lines_out.append(_ln)
    text = "".join(_lines_out)

    # 11c. Wrap bare http(s) URLs in <> for CommonMark autolink.
    # Group 1 = full [text](url) link → pass through; else wrap bare URL.
    def _autolink_repl(m: re.Match) -> str:
        if m.group(1):
            return m.group(1)
        url = m.group(0)
        trail = ""
        while url and url[-1] in ".,;:!?)":
            trail = url[-1] + trail
            url = url[:-1]
        return f"<{url}>{trail}" if url else m.group(0)
    text = re.sub(
        r'(\[[^\]]*\]\([^)]*\))|(?<!\]\()(?<![<"])https?://\S+',
        _autolink_repl, text)

    # Depth-relative prefix for contrib_modules/ URLs (html_extra_path output).
    _depth = docname.count("/") if docname else 0
    _contrib_url_prefix = ("../" * _depth) + "contrib_modules/"

    def _emit_contrib_img(rel_url: str, alt: str) -> str:
        src = _contrib_url_prefix + rel_url
        return f'<img src="{src}" alt="{alt}"/>'

    # 12. Image paths "images/foo.png" — resolve like Doxygen's flat IMAGE_PATH:
    #     prefer the doc's own "images/" sibling, then fall back to a global
    #     basename lookup across every tutorial `images/` folder. As a final
    #     fallback, point at the consolidated `tutorials/others/images/` dir
    #     (where modules like `photo` store their assets).
    def _img_repl(m: re.Match) -> str:
        alt, rel = m.group("alt"), m.group("rel")
        if docname:
            local = DOC_ROOT / pathlib.Path(docname).parent / "images" / rel
            if local.is_file():
                return m.group(0)
        hit = _IMAGE_INDEX.get(pathlib.Path(rel).name)
        if hit:
            if hit.startswith("contrib_modules/"):
                return _emit_contrib_img(hit[len("contrib_modules/"):], alt)
            return f'![{alt}](/{hit})'
        if docname and docname.startswith("js_tutorials/"):
            return m.group(0)
        return f'![{alt}](/tutorials/others/images/{rel})'
    text = re.sub(
        r'!\[(?P<alt>[^\]]*)\]\(images/(?P<rel>[^)]+)\)',
        _img_repl, text)

    # 12a2. "pics/foo.png" — contrib modules use pics/ instead of images/.
    def _pics_img_repl(m: re.Match) -> str:
        alt = m.group("alt")
        hit = _IMAGE_INDEX.get(pathlib.Path(m.group("rel")).name)
        if hit:
            if hit.startswith("contrib_modules/"):
                return _emit_contrib_img(hit[len("contrib_modules/"):], alt)
            return f'![{alt}](/{hit})'
        return m.group(0)
    text = re.sub(
        r'!\[(?P<alt>[^\]]*)\]\(pics/(?P<rel>[^)]+)\)',
        _pics_img_repl, text)

    # 12b. Bare image filenames with no directory prefix (e.g. "psf.png") that
    #      Doxygen resolves via IMAGE_PATH but Sphinx cannot find as-is.
    #      Redirect to images/<name> when the file lives in the doc's own
    #      images/ sibling, otherwise fall back to the global index.
    def _bare_img_repl(m: re.Match) -> str:
        rel = m.group("rel")
        if docname:
            local = DOC_ROOT / pathlib.Path(docname).parent / "images" / rel
            if local.is_file():
                return f'{m.group("pre")}images/{rel})'
        hit = _IMAGE_INDEX.get(rel)
        if hit:
            return f'{m.group("pre")}/{hit})'
        return m.group(0)
    text = re.sub(
        r'(?P<pre>!\[[^\]]*\]\()(?P<rel>[A-Za-z0-9_.-]+\.[A-Za-z]{2,4})\)',
        _bare_img_repl, text)

    # 12c. Standalone ![Caption](path) → {figure} so alt text is a visible caption.
    text = re.sub(
        r"^([ \t]*)!\[(?P<alt>[^\]]+)\]\((?P<path>[^)\n]+)\)[ \t]*$",
        lambda m: (
            f"\n{m.group(1)}```{{figure}} {m.group('path')}\n"
            f"{m.group(1)}{m.group('alt').strip()}\n"
            f"{m.group(1)}```\n"
        ),
        text, flags=re.MULTILINE)

    # 12d. Doxygen ^ rowspan cell → merge into row above via <hr class="cv-rowdiv">.
    def _merge_caret_rows(src: str) -> str:
        lines = src.split("\n")
        out: list[str] = []
        for line in lines:
            stripped = line.strip()
            if stripped.startswith("|") and "|" in stripped[1:]:
                cells = [c.strip() for c in stripped.strip("|").split("|")]
                if "^" in cells:
                    prev_idx = next(
                        (k for k in range(len(out) - 1, -1, -1)
                         if out[k].strip().startswith("|") and
                            not re.match(r"^\|[\s|:-]+\|$", out[k].strip())),
                        None)
                    if prev_idx is not None:
                        prev_cells = [c.strip() for c in
                                      out[prev_idx].strip().strip("|").split("|")]
                        merged = []
                        for j, cell in enumerate(cells):
                            pv = prev_cells[j] if j < len(prev_cells) else ""
                            if cell == "^":
                                merged.append(pv)
                            else:
                                sep = '<hr class="cv-rowdiv">'
                                merged.append(f"{pv}{sep}{cell}" if pv else cell)
                        out[prev_idx] = "| " + " | ".join(merged) + " |"
                        continue
            out.append(line)
        return "\n".join(out)
    text = _merge_caret_rows(text)

    # 13. Front-matter table: OpenCV tutorials use the "| -: | :- |"
    #     alignment pattern for the Original-author/Compatibility block.
    #     Wrap it in a {div} carrying .opencv-meta-table so custom.css
    #     can pin the rounded card + label-column styling without us
    #     modifying the .markdown source.
    def _wrap_front_matter(src: str) -> str:
        pat = re.compile(
            r"(^\|[^\n]*\|[ \t]*\n"     # header row (often empty)
            r"\|[ \t]*-:[ \t]*\|[ \t]*:-[ \t]*\|[ \t]*\n"  # alignment row
            r"(?:\|[^\n]*\|[ \t]*\n)+)",  # one or more body rows
            re.MULTILINE)
        def repl(m: re.Match) -> str:
            return f":::{{div}} opencv-meta-table\n\n{m.group(1)}\n:::\n"
        return pat.sub(repl, src, count=1)
    text = _wrap_front_matter(text)

    return text


def _source_read(app, docname, source):
    if not (docname.startswith("tutorials/") or docname.startswith("js_tutorials/")
            or docname.startswith("py_tutorials/") or docname.startswith("tutorials_contrib/")
            or docname.startswith("api/")):
        return
    source[0] = _translate(source[0], docname)
    if docname == "tutorials/tutorials" and DOC_JS_MODULES:
        source[0] += (
            "\n\n```{toctree}\n:maxdepth: 1\n:caption: JavaScript Tutorials\n\n"
            "/js_tutorials/js_tutorials\n```\n"
        )
    if docname == "tutorials/tutorials" and DOC_PY_MODULES:
        source[0] += (
            "\n\n```{toctree}\n:maxdepth: 1\n:caption: Python Tutorials\n\n"
            "/py_tutorials/py_tutorials\n```\n"
        )
    if docname == "tutorials/tutorials" and CONTRIB_MODULES:
        source[0] += (
            "\n\n```{toctree}\n:maxdepth: 1\n:caption: Contrib Tutorials\n\n"
            "/tutorials_contrib/contrib_root\n```\n"
        )
    if docname == "tutorials/tutorials" and API_MODULES \
            and (SPHINX_INPUT_ROOT / "api" / "library_root.rst").exists():
        source[0] += (
            "\n\n```{toctree}\n:maxdepth: 1\n:caption: API Reference\n\n"
            "/api/library_root\n```\n"
        )


def _patch_cpp_xref_resolver():
    """Guard the C++ domain's xref resolver against an upstream assertion on
    template-class cross-references. Sphinx 8.1.x asserts `parentSymbol`
    inside `_resolve_xref_inner`; some breathe-emitted class-page xrefs
    (e.g. `cv::Affine3<T>`) trigger that path with no parent symbol.
    Treat it as an unresolved xref instead of crashing the whole build."""
    try:
        from sphinx.domains.cpp import CPPDomain
    except ImportError:
        return
    original = CPPDomain._resolve_xref_inner

    def guarded(self, env, fromdocname, builder, typ, target, node, contnode):
        try:
            return original(self, env, fromdocname, builder, typ, target,
                            node, contnode)
        except AssertionError:
            return None, None
    CPPDomain._resolve_xref_inner = guarded


_patch_cpp_xref_resolver()


def _silence_breathe_anon_enum_warning():
    """Suppress the docutils "Invalid C++ declaration: Expected identifier
    in nested name." warning that breathe triggers when rendering an
    *anonymous* nested enum inside a struct (e.g. `cv::MatShape` has an
    enum whose `<name>` element is empty — Doxygen XML allows it, but the
    Sphinx C++ domain parser rejects the resulting declaration).

    The render is otherwise fine (the enum values still appear); only the
    parse-time warning is noise. We filter it via a Python logging filter
    rather than monkey-patching the parser so the same fix survives Sphinx
    version bumps."""
    import logging
    class _AnonEnumFilter(logging.Filter):
        def filter(self, record: logging.LogRecord) -> bool:
            msg = record.getMessage()
            return not (
                ("Invalid C++ declaration" in msg
                 and "Expected identifier in nested name" in msg)
                or
                ("Duplicate C++ declaration" in msg
                 and "cpp:None::" in msg)
            )
    # docutils warning messages route through both 'sphinx' and 'docutils'
    # loggers depending on entry point; attach to both for coverage.
    for _name in ("sphinx", "docutils"):
        logging.getLogger(_name).addFilter(_AnonEnumFilter())


_silence_breathe_anon_enum_warning()


def setup(app):
    app.connect("source-read", _source_read)
    return {"parallel_read_safe": True, "parallel_write_safe": True}
