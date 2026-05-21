# OpenCV Docs Modernization Reference

This document describes every technical change made when converting OpenCV tutorials from the legacy Doxygen-based `doc/` system to the modern Sphinx/MyST `docs_modern/` system. Use it as a lookup guide when porting tutorials or debugging conversion output.

---

## 1. Overview

| Aspect | Old (`doc/`) | New (`docs_modern/`) |
|--------|-------------|----------------------|
| **Generator** | Doxygen only | Doxygen → XML → Sphinx |
| **File format** | `.markdown` with Doxygen directives | `.md` with MyST directives |
| **Theme** | doxygen-awesome-css | pydata-sphinx-theme 0.17.1 |
| **Body font** | System default | Inter (Google Fonts) |
| **Code font** | System monospace | JetBrains Mono (Google Fonts) |
| **Primary color** | `#1779c4` | `#0066cc` (light) / `#539bf5` (dark) |
| **Dark mode** | None | Native toggle (localStorage) |
| **Custom CSS** | 15 lines | 976 lines |
| **Math renderer** | MathJax 2 | MathJax 3 |

---

## 2. Documentation Generator

**Old system:** Doxygen reads `.markdown` source files and generates HTML directly. Config lives in `doc/Doxyfile.in` and `doc/DoxygenLayout.xml`.

**New system:** A two-stage pipeline.

1. **Doxygen** runs first and produces only XML (not HTML) from C++ headers. This XML is the API reference data.
2. **Sphinx 8.1.3** takes over:
   - **Breathe 4.36.0** reads the Doxygen XML and makes the C++ API available as Sphinx directives.
   - **Exhale 0.3.7** auto-generates the `api/` RST file tree from that data.
   - MyST-Parser 4.0.1 parses `.md` tutorial files as CommonMark + directives.
   - Sphinx produces the final HTML output.

The human-authored tutorial files (`.md`) are written entirely for Sphinx/MyST — they contain no Doxygen syntax.

---

## 3. File Format

**Old:** `.markdown` files. The format is mostly Markdown but uses Doxygen-specific `@directive` syntax for admonitions, code snippets, cross-references, math, and navigation.

**New:** `.md` files using **MyST-Parser 4.0.1**. MyST is standard CommonMark Markdown extended with `:::{directive}` block syntax (the "colon fence" style). All Doxygen `@directives` are replaced by equivalent MyST constructs.

---

## 4. Conversion Tool: `_tools/dox2myst.py`

This is a 1,340-line Python script that automates the conversion of a single `.markdown` tutorial into the new MyST `.md` format. **It is not run during the Sphinx build** — contributors run it manually when porting each tutorial page.

### Running the converter

```bash
# Step 1: download the tagfile once (needed for @ref resolution)
curl -sSL -o /tmp/opencv.tag https://docs.opencv.org/5.x/opencv.tag

# Step 2: convert a tutorial
python3 docs_modern/_tools/dox2myst.py \
    doc/tutorials/calib3d/camera_calibration.markdown \
    docs_modern/tutorials/cpp/calib3d/camera_calibration.md \
    --tag /tmp/opencv.tag \
    --local docs_modern/_tools/local_refs.json \
    --out-doc tutorials/cpp/calib3d/camera_calibration
```

### `--tag` (tagfile)

The script resolves every `@ref SomeClass` or `@ref tutorial_id` by looking it up in the Doxygen XML tagfile downloaded from `docs.opencv.org`. If found, it becomes a link to `https://docs.opencv.org/5.x/...`. If the target is a page hosted locally in `docs_modern/`, the `--local` map redirects it to a relative Markdown link instead.

### `--local` (`local_refs.json`)

A JSON dictionary mapping Doxygen anchor IDs to Sphinx doc paths:

```json
{
  "tutorial_camera_calibration": "tutorials/cpp/calib3d/camera_calibration",
  "tutorial_py_table_of_contents_core": "tutorials/python/core/index"
}
```

Add a new entry every time a tutorial is ported so that `@ref` cross-links from other pages point to the local copy. **This file must be updated with each new ported page.**

### Frontmatter preservation

If the output `.md` file already starts with `---...---` YAML frontmatter, the script keeps it intact and appends the converted content after it. This lets you pre-write frontmatter (e.g. `orphan: true`) without it being overwritten on re-run.

---

### Full transformation pipeline (in execution order)

1. **Title normalization** — `Title {#anchor}\n=====` → `# Title`; all sub-headings shifted down one level to compensate
2. **Code fence language cleanup** — strip leading dot from language tag (`` ```{.cpp} `` → ` ```cpp `)
3. **Code fence dedent** — remove the common leading whitespace from every line inside a fenced block
4. **Directive removals** — `@tableofcontents`, `[TOC]`, `@cond`/`@endcond` are deleted (Sphinx handles TOC automatically)
5. **Anchor conversion** — `@anchor id` → `<a id="id">` (underscores replaced with hyphens)
6. **Verbatim stash** — `@verbatim...@endverbatim` blocks are swapped with a placeholder token so later passes do not process their content; restored at step 33
7. **Inline link-def inlining** — `[label]: url` reference-definition lines → inline `[label](url)` at each usage site
8. **Admonition `@parblock`** — `@note @parblock...@endparblock` (multi-paragraph note) → single `:::{note}...:::` block
9. **`\n` escaped newlines** → `<br>`; standalone `<br>`-only lines are then deleted
10. **Language toggle conversion** — consecutive `@add_toggle_X...@end_toggle` blocks → `::::{tab-set}` with one `:::tab-item` per language; a single-language toggle is unwrapped (content kept, tab wrapper removed); empty toggle pairs are deleted entirely
11. **`@prev_tutorial` / `@next_tutorial`** → deleted (Sphinx generates previous/next automatically)
12. **Sub-heading anchors** — `Section {#id}\n-----` → MyST label `(id)=\n## Section`
13. **Adjacent display math** — `\f]\f[` (two display-math blocks written back-to-back) → separated with a blank line between them
14. **`\bordermatrix`** → `\begin{array}{...}` equivalent
15. **Display math** — `\f[...\f]` → `$$\n...\n$$`
16. **Inline math** — `\f$...\f$` → `$...$`
17. **Italic star escaping** — a multiplication `*` inside an italic `*...*` span is escaped so CommonMark does not misparse the span boundaries
18. **Underline-style H2** — `Text\n-----` → `## Text`
19. **HTML headings** — `<h1>...<h6>` inline HTML → ATX `#` headings
20. **Numbered steps** — `-# item` (Doxygen auto-number) → `1. item` with continuation indentation adjusted
21. **`@snippet`** → ` ```{doxysnippet}` directive with language auto-detected from file extension
22. **`@include`** → ` ```{doxyinclude}` directive with language auto-detected from file extension
23. **Multi-line admonitions** — `@note`, `@warning`, `@attention`, `@important`, `@tip` and their continuation lines collected into `:::{kind}...:::` blocks via indent heuristic
24. **`@see`** → `:::{seealso}\n- item\n:::`
25. **`@youtube`** → responsive raw HTML `<iframe>` block (see [YouTube Embeds](#17-youtube-embeds))
26. **`@cite`** → Markdown link to the citelist page on `docs.opencv.org`
27. **`@ref` / `@subpage` inside an existing link** — URL-only substitution, label unchanged
28. **`@ref` / `@subpage` standalone** → `[label](url)` using tagfile + local_refs, or `` `anchor_id` `` backtick fallback if the symbol is not found
29. **Image path prefix** — bare `file.png` image filenames without a directory → `images/file.png`
30. **Auto-linking cv:: symbols** — bare `cv::ClassName`, `#MACRO_NAME`, `CV_MACRO_NAME` in paragraph text → linked to the API docs page via the tagfile; existing links, inline code, math, and fences are protected and not touched
31. **`%` no-link prefix removal** — `%cv::Foo` (Doxygen suppress-link convention) → `%` stripped, no link generated
32. **Adjacent admonition merge** — two identical admonition types written one after the other with no content between them are joined into a single block
33. **Verbatim restore** — the stashed verbatim placeholders are put back as plain ` ``` ``` ` fenced blocks
34. **Orphan code fence indent fix** — a fenced block at column 0 surrounded by list-item continuation lines is indented to match the list continuation column so it stays inside the list
35. **Over-indented sub-bullet fix** — sub-bullets inside a `- ` parent that are at column 8 or higher (which CommonMark treats as an indented code block) are de-indented to column 4
36. **Module bullet → list-table** — runs of `- [Title](url) (**name**) - description` bullets → `opencv-module-table` `list-table` directive
37. **Rowspan/colspan table → HTML** — tables with `^` rowspan markers or empty cells implying spanning → raw HTML `<table class="opencv-rowspan-table">` in a scrollable `<div>`
38. **Top-level numbered list de-indent** — `    1. item` (4-space-indented after a plain paragraph) → `1. item` so CommonMark does not misread it as an indented code block
39. **Images → figures** — `![alt text](url)` with a non-empty alt → `{figure}` directive block with alt and caption text
40. **Metadata table wrap** — the first table in a file that has an `Original author` or `Compatibility` row → wrapped in `:::{div} opencv-meta-table`
41. **Blank lines around images** — standalone image lines get blank lines inserted before and after
42. **Blank lines around `$$`** — every `$$` display-math delimiter gets blank lines before and after to prevent MyST misparsing
43. **Collapse excess blank lines** — three or more consecutive blank lines → two blank lines
44. **Existing frontmatter preserved** — if the output file already begins with `---...---` YAML, that block is kept at the top

---

### Known limitations — new rules may be needed as more pages are ported

- `@brief`, `@param`, `@return`, `@since`, `@throws`, `@todo` are **silently deleted** — not yet mapped to any MyST equivalent; pages that use them heavily need manual cleanup
- `@code` without a language tag defaults to `cpp`; pages with pseudocode or plain text inside `@code` blocks need the language changed manually
- Rowspan/colspan detection uses an indent heuristic; unusual table layouts may produce wrong HTML and need hand-editing
- Auto-linking skips symbols shorter than 2 characters and ignores common C++ keywords; unusual symbol names may escape linkification
- `@htmlonly ... @endhtmlonly` blocks are not yet handled
- `@latexonly ... @endlatexonly` blocks are not yet handled
- `@dontinclude` + `@skip` / `@until` (multi-step snippet extraction) is not yet handled
- Doxygen `\rst ... \endrst` embedded RST blocks are not yet handled
- Multi-level nested admonitions (an `@note` inside a list inside another `@note`) may mis-detect paragraph boundaries

---

## 5. Theme & Fonts

| | Old | New |
|-|-----|-----|
| **Theme** | doxygen-awesome-css (67 KB CSS) | pydata-sphinx-theme 0.17.1 |
| **Body font** | System default | Inter, loaded from Google Fonts |
| **Code font** | System monospace | JetBrains Mono, loaded from Google Fonts |
| **Custom CSS** | `doc/custom.css` — 15 lines | `docs_modern/_static/custom.css` — 976 lines |

---

## 6. Color & Dark Mode

**Light mode:** links `#0066cc`, accent `#003a6b`

**Dark mode:** links `#539bf5`, accent `#a4c9ff`

Dark mode is toggled by a sun/moon button in the navbar. The choice is saved to `localStorage` under the key `opencv-theme` and also respects the system `prefers-color-scheme` media query. The old docs had no dark mode.

All color rules in `_static/custom.css` are paired — a light-mode rule and a matching `html[data-theme="dark"]` rule.

---

## 7. Table Styling

**Old:** Bare Doxygen pipe tables with almost no CSS.

**New:** Three distinct table classes, each with custom CSS:

**`.opencv-meta-table`** (author/compatibility block at the top of each tutorial):
- `border-collapse: separate`
- `border-spacing: 0`
- `border-radius: 8px` on the container (rounded corners)
- Compact cell padding: `6px 12px`
- Subtle hover row highlight
- Written in source as `:::{div} opencv-meta-table` wrapping a standard Markdown table

**`.opencv-module-table`** (module listing on index pages):
- `list-table` directive with column widths `22` (module link) and `78` (description)
- Row hover highlight
- Auto-generated from `- [Title](url) (**name**) - desc` bullet pattern by `dox2myst.py`

**`.opencv-rowspan-table`** (tables with merged cells):
- Tables that use `^` rowspan markers or implicit empty-cell spanning are converted to raw HTML by `dox2myst.py`
- Wrapped in `<div class="pst-scrollable-table-container">` for horizontal scroll on narrow screens

---

## 8. Title & Heading Syntax

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `Title {#ref_id}`<br>`===========` | `# Title` |
| `Section`<br>`---------` (underline) | `## Section` |
| `Subsection`<br>`~~~~~~~~~~` | `### Subsection` |
| `<h3>Heading</h3>` inline HTML | `### Heading` |
| Heading with anchor: `Title {#my_id}`<br>`===========` | MyST label + heading:<br>`(my_id)=`<br>`# Title` |

---

## 9. Admonitions

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `@note text` | `:::{note}`<br>`text`<br>`:::` |
| `@warning text` | `:::{warning}`<br>`text`<br>`:::` |
| `@attention text` | `:::{attention}`<br>`text`<br>`:::` |
| `@important text` | `:::{important}`<br>`text`<br>`:::` |
| `@tip text` | `:::{tip}`<br>`text`<br>`:::` |
| `@see SomeClass` | `:::{seealso}`<br>`- SomeClass`<br>`:::` |
| Multi-paragraph `@note`:<br>`@note @parblock`<br>`para1`<br><br>`para2`<br>`@endparblock` | `:::{note}`<br>`para1`<br><br>`para2`<br>`:::` |
| Two `@note` blocks written back-to-back with no text between them | Combined into a single `:::{note}` box |

---

## 10. Math

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| Inline: `` \f$x^2\f$ `` | Inline: `$x^2$` |
| Display: `\f[`<br>`x = y`<br>`\f]` | Display: `$$`<br>`x = y`<br>`$$` |
| Two adjacent display blocks: `\f]\f[` | Two separate `$$...$$` blocks with a blank line between |
| `\bordermatrix{...}` | `\begin{array}{...}` equivalent |
| MathJax 2 | MathJax 3 |

Every `$$` delimiter must have a blank line before and after it — the converter enforces this automatically to prevent MyST from misparsing adjacent inline math.

---

## 11. Language Toggles (Tabs)

**Old:**

```
@add_toggle_cpp
C++ code here
@end_toggle

@add_toggle_python
Python code here
@end_toggle
```

**New:**

```
::::{tab-set}
:::{{tab-item}} C++
:sync: cpp

C++ code here
:::

:::{{tab-item}} Python
:sync: python

Python code here
:::
::::
```

**Rules:**
- A toggle block with only one language is **unwrapped** — the content is kept but the tab-set wrapper is removed
- Empty toggle pairs (`@add_toggle_X` immediately followed by `@end_toggle`) are **deleted**
- The `:sync:` key (`cpp`, `python`, `java`) connects all tab-sets on the page: clicking "Python" in one code block switches every code block on the page to Python

---

## 12. Code Blocks

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `@code{.cpp}`<br>`...`<br>`@endcode` | ` ```cpp`<br>`...`<br>` ``` ` |
| `@code` (no language) | ` ```cpp` (defaults to cpp) |
| `@verbatim`<br>`...`<br>`@endverbatim` | ` ``` `<br>`...`<br>` ``` ` (no language tag) |
| Language written as `.cpp` in fence | Leading dot stripped → `cpp` |
| Code fence at column 0 inside a list item | Indented to match the list continuation column |
| Sub-bullets at column 8+ inside a parent `- ` item | De-indented to column 4 (CommonMark would treat col 8+ as a code block) |

---

## 13. Code Snippets & Includes

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `@snippet samples/cpp/foo.cpp snippet_tag` | ` ```{doxysnippet} samples/cpp/foo.cpp`<br>`:tag: snippet_tag`<br>`:language: cpp`<br>` ``` ` |
| `@include samples/cpp/foo.cpp` | ` ```{doxyinclude} samples/cpp/foo.cpp`<br>`:language: cpp`<br>` ``` ` |

**Language auto-detected from file extension:**

| Extension | Language |
|-----------|----------|
| `.cpp`, `.hpp`, `.h`, `.cxx` | `cpp` |
| `.py` | `python` |
| `.js` | `javascript` |
| `.java` | `java` |
| `.sh`, `.bash` | `bash` |
| `.cmake` | `cmake` |
| `.xml` | `xml` |
| `.yaml`, `.yml` | `yaml` |

**Snippet markers in source files** (unchanged — same as Doxygen):
```cpp
//! [snippet_tag]
cv::Mat img = cv::imread("image.png");
//! [snippet_tag]
```

Handled by `_ext/doxysnippet.py`. See [Custom Sphinx Extensions](#21-custom-sphinx-extensions) for details.

---

## 14. Cross-References

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `@ref cv::Mat` | `[cv::Mat](https://docs.opencv.org/5.x/...)` |
| `@ref tutorial_camera_calibration` (local page) | `[Title](../calib3d/camera_calibration.md)` (via `local_refs.json`) |
| `@ref unknown_id` (not found anywhere) | `` `unknown_id` `` (backtick fallback) |
| `@subpage page_id` | Same resolution as `@ref` |
| `[text](@ref SomeClass)` inside existing link | URL replaced, label unchanged |
| `cv::Mat` bare in paragraph text | `[cv::Mat](https://docs.opencv.org/5.x/...)` (auto-linked) |
| `` `#CV_8U` `` inline code | `[CV_8U](url)` if found in tagfile |
| `CV_8U` bare in paragraph text | `[CV_8U](url)` if found in tagfile |
| `%cv::Mat` (Doxygen no-link prefix) | `cv::Mat` with no link — the `%` is stripped |

**Resolution order** used by the converter:
1. `local_refs.json` (local Sphinx pages)
2. `docanchors` in the tagfile
3. `pages` in the tagfile
4. `groups` in the tagfile
5. `by_name` (classes, structs, members)
6. Retry with `cv::` prefix prepended
7. Backtick fallback

---

## 15. Navigation & Metadata

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `@prev_tutorial{id}` | Deleted — Sphinx generates prev/next automatically |
| `@next_tutorial{id}` | Deleted |
| `@tableofcontents` | Deleted — Sphinx generates TOC |
| `[TOC]` | Deleted |
| `@cond LANG` / `@endcond` | Deleted |
| `@anchor my_id` | `<a id="my-id"></a>` (underscores → hyphens) |
| Author/compatibility Markdown table at page top | Wrapped in `:::{div} opencv-meta-table` block |

---

## 16. Images & Figures

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `![Result](images/result.jpg)` (with alt text) | `:::{{figure}} images/result.jpg`<br>`:alt: Result`<br><br>`Result`<br>`:::` |
| `![](images/result.jpg)` (no alt text) | Kept as inline `![](images/result.jpg)` |
| `![alt](result.jpg)` (no `images/` prefix) | Path auto-prefixed → `![alt](images/result.jpg)` |
| Image inline with no blank lines around it | Blank lines inserted before and after automatically |

---

## 17. YouTube Embeds

**Old:**
```
@youtube{dTBCOAKgcOQ}
```

**New** (responsive iframe):
````
```{raw} html
<div class="responsive-iframe"
     style="position:relative;padding-bottom:56.25%;height:0;
            overflow:hidden;max-width:100%;margin:1.5rem 0;">
  <iframe style="position:absolute;top:0;left:0;width:100%;height:100%;border:0;"
          src="https://www.youtube-nocookie.com/embed/dTBCOAKgcOQ?rel=0"
          title="YouTube video"
          allow="accelerometer; autoplay; clipboard-write; encrypted-media;
                 gyroscope; picture-in-picture"
          allowfullscreen></iframe>
</div>
```
````

---

## 18. Citations

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `@cite Hartley2004` | `[\[Hartley2004\]](https://docs.opencv.org/5.x/d0/de3/citelist.html#CITEREF_Hartley2004)` |

---

## 19. Numbered Lists

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `-# First step` | `1. First step` |
| `-# Second step` | `1. Second step` |
| `    -# nested step` | `    1. nested step` |
| `    1. item` (4-space-indented after a paragraph) | `1. item` (de-indented; 4-space indent means code block in CommonMark) |

---

## 20. Inline Formatting

| Old (Doxygen) | New (MyST) |
|---------------|-----------|
| `\n` escaped newline in text | `<br>` |
| Standalone `<br>` on its own line | Deleted |
| `*a * b*` (multiplication star inside italic) | `*a \* b*` (star escaped) |
| `[label]: url` reference definition | Inlined as `[label](url)` everywhere it is used |

---

## 21. Custom Sphinx Extensions

Three custom extensions live in `docs_modern/_ext/`. They are loaded by `conf.py` and provide directives that have no equivalent in standard Sphinx.

---

### `_ext/tabs.py` — Tab Set Directive

Turns `:::tab-set` / `:::tab-item` MyST blocks into a synchronized HTML tab UI. Readers can switch all code examples on a page between C++, Python, and Java with a single click.

**Directives provided:**

| Directive | Usage | HTML output |
|-----------|-------|-------------|
| `div` | `:::{div} classname` | `<div class="classname">` |
| `tab-set` | `::::{tab-set}` | `<div class="ocv-tabset">` with label bar + panels |
| `tab-item` | `:::{{tab-item}} Label` / `:sync: key` | One `<button class="ocv-tab-btn">` + one `<div class="ocv-tab-panel">` |

**How tab sync works:**

Each `tab-item` gets a `data-sync` key (e.g. `cpp`, `python`). When the user clicks a tab button, the JS fires `ocvTabClick` with that key. Every other `ocv-tabset` on the page that has a panel with the same sync key switches to show it. Clicking "Python" in one code block instantly switches all code blocks on the page.

**HTML structure emitted:**

```html
<div class="ocv-tabset">
  <div class="ocv-tab-labels">
    <button class="ocv-tab-btn active" data-sync="cpp"
            onclick="ocvTabClick(this)">C++</button>
    <button class="ocv-tab-btn" data-sync="python"
            onclick="ocvTabClick(this)">Python</button>
  </div>
  <div class="ocv-tab-panel active" data-sync="cpp">...</div>
  <div class="ocv-tab-panel" data-sync="python">...</div>
</div>
```

**CSS classes used:** `.ocv-tabset`, `.ocv-tab-labels`, `.ocv-tab-btn`, `.ocv-tab-btn.active`, `.ocv-tab-panel`, `.ocv-tab-panel.active` — all defined in `_static/custom.css`.

---

### `_ext/doxysnippet.py` — Code Snippet / Include Directive

Extracts labelled code regions from OpenCV source files and embeds them in the docs. Uses the same `//! [tag]` marker system Doxygen uses, so the live source code in the repo is always what appears in the documentation.

**Directives provided:**

| Directive | Purpose |
|-----------|---------|
| `{doxysnippet}` | Extract the region between two matching `//! [tag]` markers |
| `{doxyinclude}` | Include an entire file |

**Options for `{doxysnippet}`:**

| Option | Description |
|--------|-------------|
| `:tag:` | Marker label (required) |
| `:language:` | Override auto-detected language |
| `:dedent:` | Spaces to strip from each line (default: auto) |

**File search order** (first match wins):

1. Path relative to OpenCV repo root
2. Under `samples/`
3. Under `apps/`
4. Under `modules/`
5. Under `doc/tutorials/`
6. Under `doc/`

**Marker comment styles recognized:**

| Language | Marker style |
|----------|-------------|
| C++, Java | `//! [tag]` |
| Python | `## [tag]` |
| Shell | `# [tag]` |
| HTML, XML | `<!-- [tag] -->` |
| Block comment | `/* [tag] */` |

---

### `_ext/opencv_code_links.py` — Symbol Auto-Linker

Makes OpenCV class names and function names inside code blocks clickable, linking to the API reference page. Works in two stages:

**Stage 1 — build time:**
After Sphinx finishes building, a `_on_build_finished` hook parses the Doxygen XML tagfile and writes `_static/opencv-symbols.json` — a dictionary mapping every public symbol name to its URL on `docs.opencv.org/5.x/`.

Symbols included: classes, structs, member functions, free functions, macros, namespaces.

Symbols excluded: anything shorter than 2 characters, and common C++ keywords (`int`, `void`, `bool`, `true`, `false`, `const`, etc.).

**Stage 2 — page load time:**
`_static/opencv-code-links.js` reads the JSON, scans Pygments-highlighted code blocks for `.nc` (class names), `.nf` (function names), and `.cpf` (preprocessor) tokens, and wraps any that match a known symbol in `<a href="...">` tags pointing to `docs.opencv.org/5.x/`.

---

## 22. Critical Files

### `docs_modern/conf.py`

Sphinx master configuration. Key settings:

- Loads `myst_parser`, `breathe`, `exhale`, and all three custom extensions from `_ext/`
- MyST features enabled: `dollarmath` (enables `$...$` math), `amsmath`, `deflist`, `colon_fence`, `fieldlist`, `tasklist`
- Theme: `pydata_sphinx_theme` with Inter + JetBrains Mono fonts, light/dark logos, navbar links to GitHub and main OpenCV docs
- Breathe `default_project` points to `_build/doxygen/xml`
- Exhale outputs auto-generated API RST to `api/`
- Sidebar excluded for: `index`, `introduction`, `faq`

---

### `docs_modern/_tools/dox2myst.py`

1,340-line CLI converter. Transforms one Doxygen `.markdown` tutorial into a MyST `.md` file. **Run manually by contributors — not part of the Sphinx build.** Full transformation pipeline documented in [Section 4](#4-conversion-tool-_toolsdox2mystpy).

---

### `docs_modern/_tools/local_refs.json`

JSON map `{ "doxygen_anchor_id": "sphinx/docname" }`. Controls whether `@ref tutorial_*` links in converted files point to a locally-hosted page or fall back to `docs.opencv.org`. **Must be extended with a new entry each time a tutorial is ported.**

---

### `docs_modern/_static/custom.css`

976-line CSS providing all visual customization on top of `pydata-sphinx-theme`:

| Section | What it does |
|---------|-------------|
| `@import` | Loads Inter and JetBrains Mono from Google Fonts |
| Color variables | CSS custom properties for accent, link, heading colors in light and dark mode |
| Typography | Explicit `font-size`, `line-height`, `letter-spacing` for `h1`–`h3` |
| `.opencv-meta-table` | Rounded corners (`border-radius: 8px`), separate borders, hover highlight |
| `.opencv-module-table` | Fixed column widths, hover highlight |
| `.doxysnippet` | Code block container styling |
| `.ocv-tabset` / `.ocv-tab-btn` / `.ocv-tab-panel` | Tab UI components |
| `html[data-theme="dark"] ...` | Full dark-mode duplicates of every rule above |

---

### `docs_modern/_ext/tabs.py`

Custom Sphinx extension providing `tab-set` / `tab-item` / `div` directives. Required because `pydata-sphinx-theme` has no built-in tab component that supports cross-page sync. See [Section 21](#21-custom-sphinx-extensions).

---

### `docs_modern/_ext/doxysnippet.py`

Custom Sphinx extension providing `{doxysnippet}` / `{doxyinclude}` directives. Replaces Doxygen's `@snippet` / `@include` mechanism so live source code is still embedded in tutorials after conversion. Requires the OpenCV source tree to be accessible at build time. See [Section 21](#21-custom-sphinx-extensions).

---

### `docs_modern/_ext/opencv_code_links.py`

Post-build hook that generates `_static/opencv-symbols.json` for client-side API linkification. Requires `opencv.tag` (downloaded automatically by `CMakeLists.txt`). See [Section 21](#21-custom-sphinx-extensions).

---

### `docs_modern/CMakeLists.txt`

52-line CMake integration file. Registers the `opencv_docs_modern` build target, downloads `opencv.tag` from `docs.opencv.org`, and wires Sphinx to the Doxygen XML output. Does not run `dox2myst.py` — that is a manual contributor step.

---

### `doc/Doxyfile.in` (old system — reference only)

413-line Doxygen config template that defines the custom Doxygen aliases (`@add_toggle_cpp`, `@youtube`, `@prev_tutorial`, `@next_tutorial`, `@snippet`, etc.) that `dox2myst.py` converts away. Useful when encountering an unknown `@directive` during manual conversion.

---

### `doc/custom.css` (old system — reference only)

Only 15 lines. Hides `.memSeparator` table cells and fixes `.toc` positioning for Doxygen 1.9.8. The contrast with the new 976-line `custom.css` shows how much visual investment the modern system adds.
