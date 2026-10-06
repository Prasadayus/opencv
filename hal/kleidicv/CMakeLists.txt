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
    include("${KLEIDICV_SOURCE_PATH}/adapters/opencv/CMakeLists.txt")
    # split/merge: KleidiCV has the kernels, its adapter does not wire them
    set(KLEIDICV_HAL_HEADERS ${KLEIDICV_HAL_HEADERS}
        "${CMAKE_CURRENT_LIST_DIR}/kleidicv_splitmerge_shim.hpp" CACHE INTERNAL "")
    # clang-cl reads KleidiCV's -Wall as -Weverything; replaying its options puts ours after it
    target_compile_options(kleidicv_hal PRIVATE
        $<TARGET_PROPERTY:kleidicv,COMPILE_OPTIONS>
        "-Wno-everything")
    # the kernels live in OBJECT libraries, so every target has to be named
    foreach(t kleidicv kleidicv_thread kleidicv_neon kleidicv_sve2 kleidicv_sme kleidicv_sme2)
      if(TARGET ${t})
        target_compile_options(${t} PRIVATE "-Wno-everything")
      endif()
    endforeach()
  endif()
endif()
