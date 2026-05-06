if(NOT BUILD_DOCS_MODERN)
  return()
endif()

find_package(Python3 COMPONENTS Interpreter)
if(NOT Python3_Interpreter_FOUND)
  message(WARNING "BUILD_DOCS_MODERN requires Python 3 — target opencv_docs_modern disabled.")
  return()
endif()

find_program(SPHINX_BUILD_EXECUTABLE NAMES sphinx-build sphinx-build-3)
if(NOT SPHINX_BUILD_EXECUTABLE)
  message(WARNING
    "BUILD_DOCS_MODERN: sphinx-build not found. Install dependencies with:\n"
    "  pip install sphinx myst-parser sphinx-design sphinx-copybutton pydata-sphinx-theme linkify-it-py sphinx-autobuild\n"
    "Target opencv_docs_modern disabled.")
  return()
endif()

set(MODERN_DOCS_SRC "${CMAKE_CURRENT_SOURCE_DIR}")
set(MODERN_DOCS_OUT "${CMAKE_BINARY_DIR}/doc/modern_html")
set(MODERN_DOCS_TAG "${CMAKE_BINARY_DIR}/doc/opencv.tag")

# The modern docs cross-link OpenCV API symbols using the upstream Doxygen
# tagfile. If the local Doxygen build hasn't produced one, fetch the
# published tagfile so reviewers don't need a full Doxygen build first.
if(NOT EXISTS "${MODERN_DOCS_TAG}")
  file(DOWNLOAD "https://docs.opencv.org/5.x/opencv.tag" "${MODERN_DOCS_TAG}"
       SHOW_PROGRESS STATUS _tag_status)
  list(GET _tag_status 0 _tag_rc)
  if(NOT _tag_rc EQUAL 0)
    message(WARNING "Failed to fetch opencv.tag — code links to API symbols may be missing.")
  endif()
endif()

add_custom_target(opencv_docs_modern
  COMMAND ${CMAKE_COMMAND} -E make_directory "${MODERN_DOCS_OUT}"
  COMMAND ${SPHINX_BUILD_EXECUTABLE} -b html -E
          -D opencv_tagfile=${MODERN_DOCS_TAG}
          "${MODERN_DOCS_SRC}" "${MODERN_DOCS_OUT}"
  WORKING_DIRECTORY "${MODERN_DOCS_SRC}"
  COMMENT "Building modern (Sphinx + MyST) OpenCV documentation"
  VERBATIM
)

add_custom_target(opencv_docs_modern_serve
  COMMAND ${Python3_EXECUTABLE} -m sphinx_autobuild
          --port 8000
          "${MODERN_DOCS_SRC}" "${MODERN_DOCS_OUT}"
  WORKING_DIRECTORY "${MODERN_DOCS_SRC}"
  COMMENT "Serving modern docs at http://127.0.0.1:8000 with live reload"
  VERBATIM
)

message(STATUS "Modern docs (Sphinx + MyST) target: opencv_docs_modern (output: ${MODERN_DOCS_OUT})")
