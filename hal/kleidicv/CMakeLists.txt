project(kleidicv_hal)

if(HAVE_KLEIDICV)
  if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # MSVC cannot build KleidiCV (-march, GNU inline asm, __fp16); build it with clang-cl
    # separately and pass KLEIDICV_SOURCE_PATH + KLEIDICV_PREBUILT_DIR. clang-cl emits
    # MSVC-ABI objects, so the .lib files link against an MSVC-built OpenCV.
    if(NOT KLEIDICV_PREBUILT_DIR OR NOT EXISTS "${KLEIDICV_PREBUILT_DIR}/kleidicv_hal.lib")
      message(FATAL_ERROR
        "On Windows, KleidiCV must be built separately with clang-cl and supplied via "
        "-DKLEIDICV_PREBUILT_DIR=<build dir containing kleidicv_hal.lib, kleidicv.lib, "
        "kleidicv_thread.lib>. Got: '${KLEIDICV_PREBUILT_DIR}'")
    endif()

    set(KLEIDICV_HAL_VERSION "26.03" CACHE INTERNAL "")
    set(KLEIDICV_HAL_LIBRARIES
      "${KLEIDICV_PREBUILT_DIR}/kleidicv_hal.lib"
      "${KLEIDICV_PREBUILT_DIR}/kleidicv.lib"
      "${KLEIDICV_PREBUILT_DIR}/kleidicv_thread.lib"
      CACHE INTERNAL "")
    set(KLEIDICV_HAL_HEADERS
      "${KLEIDICV_SOURCE_PATH}/adapters/opencv/kleidicv_hal.h" CACHE INTERNAL "")
    set(KLEIDICV_HAL_INCLUDE_DIRS
      "${KLEIDICV_SOURCE_PATH}/adapters/opencv"
      "${KLEIDICV_SOURCE_PATH}/kleidicv/include"
      "${KLEIDICV_SOURCE_PATH}/kleidicv_thread/include"
      "${KLEIDICV_PREBUILT_DIR}/include"
      CACHE INTERNAL "")
  else()
    option(KLEIDICV_ENABLE_SME2 "" OFF) # not compatible with some CLang versions in NDK
    option(KLEIDICV_USE_CV_NAMESPACE_IN_OPENCV_HAL "" OFF)
    # third-party warnings; KleidiCV passes -Wall, which clang-cl reads as -Weverything
    ocv_warnings_disable(CMAKE_CXX_FLAGS
        -Wno-sign-conversion -Wno-unsafe-buffer-usage -Wno-unsafe-buffer-usage-in-libc-call
        -Wno-reserved-identifier -Wno-reserved-macro-identifier
        -Wno-deprecated-copy-with-user-provided-copy -Wno-ctad-maybe-unsupported
        -Wno-unused-template -Wno-undefined-func-template -Wno-implicit-int-conversion
        -Wno-zero-as-null-pointer-constant -Wno-shorten-64-to-32 -Wno-switch-default
        -Wno-missing-variable-declarations -Wno-unknown-argument
        -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-c++98-compat-extra-semi
        -Wno-c++98-compat-local-type-template-args -Wno-c++98-c++11-compat-binary-literal
        -Wno-pre-c++14-compat -Wno-pre-c++17-compat)
    include("${KLEIDICV_SOURCE_PATH}/adapters/opencv/CMakeLists.txt")
    # split/merge: KleidiCV has the kernels, its adapter does not wire them
    set(KLEIDICV_HAL_HEADERS ${KLEIDICV_HAL_HEADERS}
        "${CMAKE_CURRENT_LIST_DIR}/kleidicv_splitmerge_shim.hpp" CACHE INTERNAL "")
    # HACK to suppress adapters/opencv/kleidicv_hal.cpp:343:12: warning: unused function 'from_opencv' [-Wunused-function]
    target_compile_options( kleidicv_hal PRIVATE
        $<TARGET_PROPERTY:kleidicv,COMPILE_OPTIONS>
        "-Wno-old-style-cast" "-Wno-unused-function"
    )
  endif()
endif()
