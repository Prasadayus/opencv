import os
import sys
from datetime import datetime

sys.path.insert(0, os.path.abspath("_ext"))

project = "OpenCV"
author = "OpenCV Team"
copyright = f"{datetime.now().year}, OpenCV Team"
release = "5.x"

extensions = [
    "myst_parser",
    "sphinx_design",
    "sphinx_copybutton",
    "doxysnippet",
    "opencv_code_links",
]

copybutton_prompt_text = r">>> |\.\.\. |\$ |# "
copybutton_prompt_is_regexp = True

source_suffix = {".md": "markdown", ".rst": "restructuredtext"}

myst_enable_extensions = [
    "dollarmath",
    "amsmath",
    "deflist",
    "colon_fence",
    "attrs_inline",
    "attrs_block",
    "fieldlist",
    "tasklist",
    "linkify",
]
myst_linkify_fuzzy_links = False
myst_heading_anchors = 4
myst_dmath_double_inline = True

mathjax3_config = {
    "tex": {
        "inlineMath": [["$", "$"], ["\\(", "\\)"]],
        "displayMath": [["$$", "$$"], ["\\[", "\\]"]],
    }
}

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

html_theme = "pydata_sphinx_theme"
html_static_path = ["_static"]
html_css_files = ["custom.css"]
html_js_files = ["opencv-code-links.js"]
html_title = "OpenCV Documentation"
html_logo = "_static/opencv-logo-white.png"
html_favicon = "_static/opencv.ico"
html_show_sourcelink = False
html_copy_source = False

html_theme_options = {
    "navbar_start": ["navbar-logo", "version-badge"],
    "navbar_center": [],
    "navbar_end": ["external-nav", "theme-switcher", "navbar-icon-links"],
    "navbar_persistent": ["search-button"],
    "header_links_before_dropdown": 7,
    "show_prev_next": True,
    "show_toc_level": 2,
    "navigation_with_keys": False,
    "use_edit_page_button": False,
    "external_links": [
        {"name": "Main Page", "url": "https://docs.opencv.org/5.x/index.html"},
        {"name": "Related Pages", "url": "https://docs.opencv.org/5.x/pages.html"},
        {"name": "Namespaces", "url": "https://docs.opencv.org/5.x/namespaces.html"},
        {"name": "Classes", "url": "https://docs.opencv.org/5.x/annotated.html"},
        {"name": "Files", "url": "https://docs.opencv.org/5.x/files.html"},
        {"name": "Examples", "url": "https://docs.opencv.org/5.x/examples.html"},
        {"name": "Java documentation", "url": "https://docs.opencv.org/5.x/javadoc/index.html"},
    ],
    "icon_links": [
        {
            "name": "GitHub",
            "url": "https://github.com/opencv/opencv",
            "icon": "fa-brands fa-github",
        },
    ],
    "logo": {
        "image_light": "_static/opencv-logo.png",
        "image_dark": "_static/opencv-logo-white.png",
        "alt_text": "OpenCV — Open Source Computer Vision Library",
    },
    "footer_start": ["copyright"],
    "footer_end": ["sphinx-version", "theme-version"],
}

html_sidebars = {
    "**": ["sidebar-nav-bs"],
    "index": [],
    "introduction": [],
    "faq": [],
}

suppress_warnings = ["myst.header"]
