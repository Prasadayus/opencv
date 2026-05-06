# OpenCV environment variables reference

### Introduction

OpenCV can change its behavior depending on the runtime environment:
- enable extra debugging output or performance tracing
- modify default locations and search paths
- tune some algorithms or general behavior
- enable or disable workarounds, safety features and optimizations

**Notes:**
- ⭐ marks most popular variables
- variables with names like this `VAR_${NAME}` describes family of variables, where `${NAME}` should be changed to one of predefined values, e.g. `VAR_TBB`, `VAR_OPENMP`, ...

#### Setting environment variable in Windows
In terminal or cmd-file (bat-file):
```bat
set MY_ENV_VARIABLE=true
C:\my_app.exe
```
In GUI:
- Go to "Settings -> System -> About"
- Click on "Advanced system settings" in the right part
- In new window click on the "Environment variables" button
- Add an entry to the "User variables" list

#### Setting environment variable in Linux

In terminal or shell script:
```sh
export MY_ENV_VARIABLE=true
./my_app
```
or as a single command:
```sh
MY_ENV_VARIABLE=true ./my_app
```

#### Setting environment variable in Python

```py
import os
os.environ["MY_ENV_VARIABLE"] = "True" # value must be a string
import cv2 # variables set after this may not have effect
```

:::{note}
This method may not work on all operating systems and/or Python distributions. For example, it works on Ubuntu Linux with system Python interpreter, but doesn't work on Windows 10 with the official Python package. It depends on the ability of a process to change its own environment (OpenCV uses `getenv` from C++ runtime to read variables).
See also:
- https://docs.python.org/3.12/library/os.html#os.environ
- https://stackoverflow.com/questions/69199708/setenvironmentvariable-does-not-seem-to-set-values-that-can-be-retrieved-by-ge
:::
### Types

- _bool_ - `1`, `True`, `true`, `TRUE` / `0`, `False`, `false`, `FALSE`
- _number_/_size_ - unsigned number, suffixes `MB`, `Mb`, `mb`, `KB`, `Kb`, `kb`
- _string_ - plain string or can have a structure
- _path_ - to file, to directory
- _paths_ - `;`-separated on Windows, `:`-separated on others

### General, core

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>OPENCV_SKIP_CPU_BASELINE_CHECK</td>
<td>bool</td>
<td rowspan="2">false</td>
<td>do not check that current CPU supports all features used by the build (baseline)</td>
</tr>
<tr>
<td>OPENCV_CPU_DISABLE</td>
<td><code>,</code> or <code>;</code>-separated</td>
<td>disable code branches which use CPU features (dispatched code)</td>
</tr>
<tr>
<td>OPENCV_SETUP_TERMINATE_HANDLER</td>
<td>bool</td>
<td rowspan="2">true (Windows)</td>
<td>use std::set_terminate to install own termination handler</td>
</tr>
<tr>
<td>OPENCV_LIBVA_RUNTIME</td>
<td>file path</td>
<td>libva for VA interoperability utils</td>
</tr>
<tr>
<td>OPENCV_ENABLE_MEMALIGN</td>
<td>bool</td>
<td>true (except static analysis, memory sanitizer, fuzzying, _WIN32?)</td>
<td>enable aligned memory allocations</td>
</tr>
<tr>
<td>OPENCV_BUFFER_AREA_ALWAYS_SAFE</td>
<td>bool</td>
<td>false</td>
<td>enable safe mode for multi-buffer allocations (each buffer separately)</td>
</tr>
<tr>
<td>OPENCV_KMEANS_PARALLEL_GRANULARITY</td>
<td>num</td>
<td>1000</td>
<td>tune algorithm parallel work distribution parameter <code>parallel_for_(..., ..., ..., granularity)</code></td>
</tr>
<tr>
<td>OPENCV_DUMP_ERRORS</td>
<td>bool</td>
<td>true (Debug or Android), false (others)</td>
<td>print extra information on exception (log to Android)</td>
</tr>
<tr>
<td>OPENCV_DUMP_CONFIG</td>
<td>bool</td>
<td>false</td>
<td>print build configuration to stderr (<code>getBuildInformation</code>)</td>
</tr>
<tr>
<td>OPENCV_PYTHON_DEBUG</td>
<td>bool</td>
<td>false</td>
<td>enable extra warnings in Python bindings</td>
</tr>
<tr>
<td>OPENCV_TEMP_PATH</td>
<td>path</td>
<td rowspan="5"><code>/tmp/</code> (Linux), <code>/data/local/tmp/</code> (Android), <code>GetTempPathA</code> (Windows)</td>
<td>directory for temporary files</td>
</tr>
<tr>
<td>OPENCV_DATA_PATH_HINT</td>
<td>paths</td>
<td>paths for findDataFile</td>
</tr>
<tr>
<td>OPENCV_DATA_PATH</td>
<td>paths</td>
<td>paths for findDataFile</td>
</tr>
<tr>
<td>OPENCV_SAMPLES_DATA_PATH_HINT</td>
<td>paths</td>
<td>paths for findDataFile</td>
</tr>
<tr>
<td>OPENCV_SAMPLES_DATA_PATH</td>
<td>paths</td>
<td>paths for findDataFile</td>
</tr>
</tbody></table></div>
```

Links:
- https://github.com/opencv/opencv/wiki/CPU-optimizations-build-options

### Logging

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>⭐ OPENCV_LOG_LEVEL</td>
<td colspan="2">string</td>
<td>logging level (see accepted values below)</td>
</tr>
<tr>
<td>OPENCV_LOG_TIMESTAMP</td>
<td>bool</td>
<td>true</td>
<td>logging with timestamps</td>
</tr>
<tr>
<td>OPENCV_LOG_TIMESTAMP_NS</td>
<td>bool</td>
<td>false</td>
<td>add nsec to logging timestamps</td>
</tr>
</tbody></table></div>
```

#### Levels
- `0`, `O`, `OFF`, `S`, `SILENT`, `DISABLE`, `DISABLED`
- `F`, `FATAL`
- `E`, `ERROR`
- `W`, `WARNING`, `WARN`, `WARNINGS`
- `I`, `INFO`
- `D`, `DEBUG`
- `V`, `VERBOSE`

### core/parallel_for
| name | type | default | description |
|------|------|---------|-------------|
| ⭐ OPENCV_FOR_THREADS_NUM | num | 0 | set number of threads |
| OPENCV_THREAD_POOL_ACTIVE_WAIT_PAUSE_LIMIT | num | 16 | tune pthreads parallel_for backend |
| OPENCV_THREAD_POOL_ACTIVE_WAIT_WORKER | num | 2000 | tune pthreads parallel_for backend |
| OPENCV_THREAD_POOL_ACTIVE_WAIT_MAIN | num | 10000 | tune pthreads parallel_for backend |
| OPENCV_THREAD_POOL_ACTIVE_WAIT_THREADS_LIMIT | num | 0 | tune pthreads parallel_for backend |
| OPENCV_FOR_OPENMP_DYNAMIC_DISABLE | bool | false | Removed in 4.13.0. Use standard [OMP_DYNAMIC](https://www.openmp.org/spec-html/5.0/openmpsu116.html) instead |

### backends
Some modules have multiple available backends, following variables allow choosing specific backend or changing default priorities in which backends will be probed (e.g. when opening a video file).

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>OPENCV_PARALLEL_BACKEND</td>
<td colspan="2">string</td>
<td>choose specific paralel_for backend (one of <code>TBB</code>, <code>ONETBB</code>, <code>OPENMP</code>)</td>
</tr>
<tr>
<td>OPENCV_PARALLEL_PRIORITY_${NAME}</td>
<td colspan="2">num</td>
<td>set backend priority, default is 1000</td>
</tr>
<tr>
<td>OPENCV_PARALLEL_PRIORITY_LIST</td>
<td colspan="2">string, <code>,</code>-separated</td>
<td>list of backends in priority order</td>
</tr>
<tr>
<td>OPENCV_UI_BACKEND</td>
<td colspan="2">string</td>
<td>choose highgui backend for window rendering (one of <code>GTK</code>, <code>GTK3</code>, <code>GTK2</code>, <code>QT</code>, <code>WIN32</code>)</td>
</tr>
<tr>
<td>OPENCV_UI_PRIORITY_${NAME}</td>
<td colspan="2">num</td>
<td>set highgui backend priority, default is 1000</td>
</tr>
<tr>
<td>OPENCV_UI_PRIORITY_LIST</td>
<td colspan="2">string, <code>,</code>-separated</td>
<td>list of highgui backends in priority order</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_PRIORITY_${NAME}</td>
<td colspan="2">num</td>
<td>set videoio backend priority, default is 1000</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_PRIORITY_LIST</td>
<td colspan="2">string, <code>,</code>-separated</td>
<td>list of videoio backends in priority order</td>
</tr>
</tbody></table></div>
```

### plugins
Some external dependencies can be detached into a dynamic library, which will be loaded at runtime (plugin). Following variables allow changing default search locations and naming pattern for these plugins.

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>OPENCV_CORE_PLUGIN_PATH</td>
<td colspan="2">paths</td>
<td>directories to search for <em>core</em> plugins</td>
</tr>
<tr>
<td>OPENCV_CORE_PARALLEL_PLUGIN_${NAME}</td>
<td colspan="2">string, glob</td>
<td>parallel_for plugin library name (glob), e.g. default for TBB is "opencv_core_parallel_tbb*.so"</td>
</tr>
<tr>
<td>OPENCV_DNN_PLUGIN_PATH</td>
<td colspan="2">paths</td>
<td>directories to search for <em>dnn</em> plugins</td>
</tr>
<tr>
<td>OPENCV_DNN_PLUGIN_${NAME}</td>
<td colspan="2">string, glob</td>
<td>parallel_for plugin library name (glob), e.g. default for TBB is "opencv_core_parallel_tbb*.so"</td>
</tr>
<tr>
<td>OPENCV_CORE_PLUGIN_PATH</td>
<td colspan="2">paths</td>
<td>directories to search for <em>highgui</em> plugins (YES it is CORE)</td>
</tr>
<tr>
<td>OPENCV_UI_PLUGIN_${NAME}</td>
<td colspan="2">string, glob</td>
<td><em>highgui</em> plugin library name (glob)</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_PLUGIN_PATH</td>
<td colspan="2">paths</td>
<td>directories to search for <em>videoio</em> plugins</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_PLUGIN_${NAME}</td>
<td colspan="2">string, glob</td>
<td><em>videoio</em> plugin library name (glob)</td>
</tr>
</tbody></table></div>
```

### OpenCL

**Note:** OpenCL device specification format is `<Platform>:<CPU|GPU|ACCELERATOR|nothing=GPU/CPU>:<deviceName>`, e.g. `AMD:GPU:`

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>OPENCV_OPENCL_RUNTIME</td>
<td colspan="2">filepath or <code>disabled</code></td>
<td>path to OpenCL runtime library (e.g. <code>OpenCL.dll</code>, <code>libOpenCL.so</code>)</td>
</tr>
<tr>
<td>⭐ OPENCV_OPENCL_DEVICE</td>
<td colspan="2">string or <code>disabled</code></td>
<td>choose specific OpenCL device. See specification format in the note above. See more details in the Links section.</td>
</tr>
<tr>
<td>OPENCV_OPENCL_RAISE_ERROR</td>
<td>bool</td>
<td>false</td>
<td>raise exception if something fails during OpenCL kernel preparation and execution (Release builds only)</td>
</tr>
<tr>
<td>OPENCV_OPENCL_ABORT_ON_BUILD_ERROR</td>
<td>bool</td>
<td>false</td>
<td>abort if OpenCL kernel compilation failed</td>
</tr>
<tr>
<td>OPENCV_OPENCL_CACHE_ENABLE</td>
<td>bool</td>
<td>true</td>
<td>enable OpenCL kernel cache</td>
</tr>
<tr>
<td>OPENCV_OPENCL_CACHE_WRITE</td>
<td>bool</td>
<td>true</td>
<td>allow writing to the cache, otherwise cache will be read-only</td>
</tr>
<tr>
<td>OPENCV_OPENCL_CACHE_LOCK_ENABLE</td>
<td>bool</td>
<td>true</td>
<td>use .lock files to synchronize between multiple applications using the same OpenCL cache (may not work on network drives)</td>
</tr>
<tr>
<td>OPENCV_OPENCL_CACHE_CLEANUP</td>
<td>bool</td>
<td>true</td>
<td>automatically remove old entries from cache (leftovers from older OpenCL runtimes)</td>
</tr>
<tr>
<td>OPENCV_OPENCL_VALIDATE_BINARY_PROGRAMS</td>
<td>bool</td>
<td>false</td>
<td>validate loaded binary OpenCL kernels</td>
</tr>
<tr>
<td>OPENCV_OPENCL_DISABLE_BUFFER_RECT_OPERATIONS</td>
<td>bool</td>
<td rowspan="2">true (Apple), false (others)</td>
<td>enable workaround for non-continuos data downloads</td>
</tr>
<tr>
<td>OPENCV_OPENCL_BUILD_EXTRA_OPTIONS</td>
<td>string</td>
<td>pass extra options to OpenCL kernel compilation</td>
</tr>
<tr>
<td>OPENCV_OPENCL_ENABLE_MEM_USE_HOST_PTR</td>
<td>bool</td>
<td>true</td>
<td>workaround/optimization for buffer allocation</td>
</tr>
<tr>
<td>OPENCV_OPENCL_ALIGNMENT_MEM_USE_HOST_PTR</td>
<td>num</td>
<td>4</td>
<td>parameter for OPENCV_OPENCL_ENABLE_MEM_USE_HOST_PTR</td>
</tr>
<tr>
<td>OPENCV_OPENCL_DEVICE_MAX_WORK_GROUP_SIZE</td>
<td>num</td>
<td>0</td>
<td>allow to decrease maxWorkGroupSize</td>
</tr>
<tr>
<td>OPENCV_OPENCL_PROGRAM_CACHE</td>
<td>num</td>
<td>0</td>
<td>limit number of programs in OpenCL kernel cache</td>
</tr>
<tr>
<td>OPENCV_OPENCL_RAISE_ERROR_REUSE_ASYNC_KERNEL</td>
<td>bool</td>
<td>false</td>
<td>raise exception if async kernel failed</td>
</tr>
<tr>
<td>OPENCV_OPENCL_BUFFERPOOL_LIMIT</td>
<td>num</td>
<td rowspan="2">1 &lt;&lt; 27 (Intel device), 0 (others)</td>
<td>limit memory used by buffer bool</td>
</tr>
<tr>
<td>OPENCV_OPENCL_HOST_PTR_BUFFERPOOL_LIMIT</td>
<td>num</td>
<td>same as OPENCV_OPENCL_BUFFERPOOL_LIMIT, but for HOST_PTR buffers</td>
</tr>
<tr>
<td>OPENCV_OPENCL_BUFFER_FORCE_MAPPING</td>
<td>bool</td>
<td>false</td>
<td>force clEnqueueMapBuffer</td>
</tr>
<tr>
<td>OPENCV_OPENCL_BUFFER_FORCE_COPYING</td>
<td>bool</td>
<td>false</td>
<td>force clEnqueueReadBuffer/clEnqueueWriteBuffer</td>
</tr>
<tr>
<td>OPENCV_OPENCL_FORCE</td>
<td>bool</td>
<td>false</td>
<td>force running OpenCL kernel even if usual conditions are not met (e.g. dst.isUMat)</td>
</tr>
<tr>
<td>OPENCV_OPENCL_PERF_CHECK_BYPASS</td>
<td>bool</td>
<td>false</td>
<td>force running OpenCL kernel even if usual performance-related conditions are not met (e.g. image is very small)</td>
</tr>
</tbody></table></div>
```

#### SVM (Shared Virtual Memory) - disabled by default

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>OPENCV_OPENCL_SVM_DISABLE</td>
<td>bool</td>
<td>false</td>
<td rowspan="4">disable SVM</td>
</tr>
<tr>
<td>OPENCV_OPENCL_SVM_FORCE_UMAT_USAGE</td>
<td>bool</td>
<td>false</td>
</tr>
<tr>
<td>OPENCV_OPENCL_SVM_DISABLE_UMAT_USAGE</td>
<td>bool</td>
<td rowspan="3">false</td>
</tr>
<tr>
<td>OPENCV_OPENCL_SVM_CAPABILITIES_MASK</td>
<td>num</td>
</tr>
<tr>
<td>OPENCV_OPENCL_SVM_BUFFERPOOL_LIMIT</td>
<td>num</td>
<td>same as OPENCV_OPENCL_BUFFERPOOL_LIMIT, but for SVM buffers</td>
</tr>
</tbody></table></div>
```

#### Links:
- https://github.com/opencv/opencv/wiki/OpenCL-optimizations

### Tracing/Profiling
| name | type | default | description |
|------|------|---------|-------------|
| ⭐ OPENCV_TRACE | bool | false | enable trace |
| OPENCV_TRACE_LOCATION | string | `OpenCVTrace` | trace file name ("${name}-$03d.txt") |
| OPENCV_TRACE_DEPTH_OPENCV | num | 1 | |
| OPENCV_TRACE_MAX_CHILDREN_OPENCV | num | 1000 | |
| OPENCV_TRACE_MAX_CHILDREN | num | 1000 | |
| OPENCV_TRACE_SYNC_OPENCL | bool | false | wait for OpenCL kernels to finish |
| OPENCV_TRACE_ITT_ENABLE | bool | true | |
| OPENCV_TRACE_ITT_PARENT | bool | false | set parentID for ITT task |
| OPENCV_TRACE_ITT_SET_THREAD_NAME | bool | false | set name for OpenCV's threads "OpenCVThread-%03d" |

#### Links:
- https://github.com/opencv/opencv/wiki/Profiling-OpenCV-Applications

### Cache
**Note:** Default tmp location is `TMPDIR%` (Windows); `$XDG_CACHE_HOME`, `$HOME/.cache`, `/var/tmp`, `/tmp` (others)
| name | type | default | description |
|------|------|---------|-------------|
| OPENCV_CACHE_SHOW_CLEANUP_MESSAGE | bool | true | show cache cleanup message |
| OPENCV_DOWNLOAD_CACHE_DIR | path | default tmp location | cache directory for downloaded files (subdirectory `downloads`) |
| OPENCV_DNN_IE_GPU_CACHE_DIR | path | default tmp location | cache directory for OpenVINO OpenCL kernels (subdirectory `dnn_ie_cache_${device}`) |
| OPENCV_OPENCL_CACHE_DIR | path | default tmp location | cache directory for OpenCL kernels cache (subdirectory `opencl_cache`) |

### dnn
**Note:** In the table below `dump_base_name` equals to `ocv_dnn_net_%05d_%02d` where first argument is internal network ID and the second - dump level.

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>OPENCV_DNN_BACKEND_DEFAULT</td>
<td>num</td>
<td>3 (OpenCV)</td>
<td>set default DNN backend, see dnn.hpp for backends enumeration</td>
</tr>
<tr>
<td>OPENCV_DNN_NETWORK_DUMP</td>
<td>num</td>
<td>0</td>
<td rowspan="2">level of information dumps, 0 - no dumps (default file name <code>${dump_base_name}.dot</code>)</td>
</tr>
<tr>
<td>OPENCV_DNN_DISABLE_MEMORY_OPTIMIZATIONS</td>
<td>bool</td>
<td>false</td>
</tr>
<tr>
<td>OPENCV_DNN_CHECK_NAN_INF</td>
<td>bool</td>
<td>false</td>
<td>check for NaNs in layer outputs</td>
</tr>
<tr>
<td>OPENCV_DNN_CHECK_NAN_INF_DUMP</td>
<td>bool</td>
<td>false</td>
<td>print layer data when NaN check has failed</td>
</tr>
<tr>
<td>OPENCV_DNN_CHECK_NAN_INF_RAISE_ERROR</td>
<td>bool</td>
<td>false</td>
<td>also raise exception when NaN check has failed</td>
</tr>
<tr>
<td>OPENCV_DNN_ONNX_USE_LEGACY_NAMES</td>
<td>bool</td>
<td>false</td>
<td>use ONNX node names as-is instead of "onnx_node!${node_name}"</td>
</tr>
<tr>
<td>OPENCV_DNN_CUSTOM_ONNX_TYPE_INCLUDE_DOMAIN_NAME</td>
<td>bool</td>
<td rowspan="2">true</td>
<td>prepend layer domain to layer types ("domain.type")</td>
</tr>
<tr>
<td>OPENCV_VULKAN_RUNTIME</td>
<td>file path</td>
<td>set location of Vulkan runtime library for DNN Vulkan backend</td>
</tr>
<tr>
<td>OPENCV_DNN_IE_SERIALIZE</td>
<td>bool</td>
<td rowspan="4">false</td>
<td>dump intermediate OpenVINO graph (default file names <code>${dump_base_name}_ngraph.xml</code>, <code>${dump_base_name}_ngraph.bin</code>)</td>
</tr>
<tr>
<td>OPENCV_DNN_IE_EXTRA_PLUGIN_PATH</td>
<td>path</td>
<td>path to extra OpenVINO plugins</td>
</tr>
<tr>
<td>OPENCV_DNN_IE_VPU_TYPE</td>
<td>string</td>
<td>Force using specific OpenVINO VPU device type ("Myriad2" or "MyriadX")</td>
</tr>
<tr>
<td>OPENCV_TEST_DNN_IE_VPU_TYPE</td>
<td>string</td>
<td>same as OPENCV_DNN_IE_VPU_TYPE, but for tests</td>
</tr>
<tr>
<td>OPENCV_DNN_INFERENCE_ENGINE_HOLD_PLUGINS</td>
<td>bool</td>
<td>true</td>
<td>always hold one existing OpenVINO instance to avoid crashes on unloading</td>
</tr>
<tr>
<td>OPENCV_DNN_INFERENCE_ENGINE_CORE_LIFETIME_WORKAROUND</td>
<td>bool</td>
<td>true (Windows), false (other)</td>
<td>another OpenVINO lifetime workaround</td>
</tr>
<tr>
<td>OPENCV_DNN_OPENCL_ALLOW_ALL_DEVICES</td>
<td>bool</td>
<td>false</td>
<td>allow running on CPU devices, allow FP16 on non-Intel device</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_CONVOLUTION_IGNORE_INPUT_DIMS_4_CHECK</td>
<td>bool</td>
<td>false</td>
<td>workaround for OpenCL backend, see https://github.com/opencv/opencv/issues/20833</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_WORKAROUND_IDLF</td>
<td>bool</td>
<td rowspan="2">true</td>
<td>another workaround for OpenCL backend</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_CONFIG_PATH</td>
<td>path</td>
<td>path to kernel configuration cache for auto-tuning (must be existing directory), set this variable to enable auto-tuning</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_DISABLE_AUTO_TUNING</td>
<td>bool</td>
<td>false</td>
<td>disable auto-tuning</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_FORCE_AUTO_TUNING</td>
<td>bool</td>
<td>false</td>
<td>force auto-tuning</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_TEST_ALL_KERNELS</td>
<td>num</td>
<td>0</td>
<td>test convolution kernels, number of iterations (auto-tuning)</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_DUMP_FAILED_RESULT</td>
<td>bool</td>
<td>false</td>
<td>dump extra information on errors (auto-tuning)</td>
</tr>
<tr>
<td>OPENCV_OCL4DNN_TUNING_RAISE_CHECK_ERROR</td>
<td>bool</td>
<td>false</td>
<td>raise exception on errors (auto-tuning)</td>
</tr>
</tbody></table></div>
```

### Tests

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>⭐ OPENCV_TEST_DATA_PATH</td>
<td colspan="2">dir path</td>
<td>set test data search location (e.g. <code>/home/user/opencv_extra/testdata</code>)</td>
</tr>
<tr>
<td>⭐ OPENCV_DNN_TEST_DATA_PATH</td>
<td>dir path</td>
<td><code>$OPENCV_TEST_DATA_PATH/dnn</code></td>
<td>set DNN model search location for tests (used by <em>dnn</em>, <em>gapi</em>, <em>objdetect</em>, <em>video</em> modules)</td>
</tr>
<tr>
<td>OPENCV_OPEN_MODEL_ZOO_DATA_PATH</td>
<td rowspan="2">dir path</td>
<td rowspan="2"><code>$OPENCV_DNN_TEST_DATA_PATH/omz_intel_models</code></td>
<td>set OpenVINO models search location for tests (used by <em>dnn</em>, <em>gapi</em> modules)</td>
</tr>
<tr>
<td>INTEL_CVSDK_DIR</td>
<td>some <em>dnn</em> tests can search OpenVINO models here too</td>
</tr>
<tr>
<td>OPENCV_TEST_DEBUG</td>
<td>num</td>
<td>0</td>
<td>debug level for tests, same as <code>--test_debug</code> (0 - no debug (default), 1 - basic test debug information, &gt;1 - extra debug information)</td>
</tr>
<tr>
<td>OPENCV_TEST_REQUIRE_DATA</td>
<td>bool</td>
<td>false</td>
<td>same as <code>--test_require_data</code> option (fail on missing non-required test data instead of skip)</td>
</tr>
<tr>
<td>OPENCV_TEST_CHECK_OPTIONAL_DATA</td>
<td>bool</td>
<td>false</td>
<td>assert when optional data is not found</td>
</tr>
<tr>
<td>OPENCV_IPP_CHECK</td>
<td>bool</td>
<td rowspan="3">false</td>
<td>default value for <code>--test_ipp_check</code> and <code>--perf_ipp_check</code></td>
</tr>
<tr>
<td>OPENCV_PERF_VALIDATION_DIR</td>
<td>dir path</td>
<td>location of files read/written by <code>--perf_read_validation_results</code>/<code>--perf_write_validation_results</code></td>
</tr>
<tr>
<td>⭐ OPENCV_PYTEST_FILTER</td>
<td>string (glob)</td>
<td>test filter for Python tests</td>
</tr>
</tbody></table></div>
```

#### Links:
* https://github.com/opencv/opencv/wiki/QA_in_OpenCV

### videoio
**Note:** extra FFmpeg options should be pased in form `key;value|key;value|key;value`, for example `hwaccel;cuvid|video_codec;h264_cuvid|vsync;0` or `vcodec;x264|vprofile;high|vlevel;4.0`

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>⭐ OPENCV_FFMPEG_CAPTURE_OPTIONS</td>
<td colspan="2">string (see note)</td>
<td>extra options for VideoCapture FFmpeg backend</td>
</tr>
<tr>
<td>⭐ OPENCV_FFMPEG_WRITER_OPTIONS</td>
<td colspan="2">string (see note)</td>
<td>extra options for VideoWriter FFmpeg backend</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_THREADS</td>
<td colspan="2">num</td>
<td>set FFmpeg thread count</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_DEBUG</td>
<td>bool</td>
<td rowspan="2">false</td>
<td>enable logging messages from FFmpeg</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_LOGLEVEL</td>
<td>num</td>
<td>set FFmpeg logging level</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_SKIP_LOG_CALLBACK</td>
<td>bool</td>
<td rowspan="2">false</td>
<td>do not install OpenCV's FFmpeg log callback (preserve default/user callback)</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_DLL_DIR</td>
<td>dir path</td>
<td>directory with FFmpeg plugin (legacy)</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_IS_THREAD_SAFE</td>
<td>bool</td>
<td>false</td>
<td>enabling this option will turn off thread safety locks in the FFmpeg backend (use only if you are sure FFmpeg is built with threading support, tested on Linux)</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_READ_ATTEMPTS</td>
<td>num</td>
<td>4096</td>
<td>number of failed <code>av_read_frame</code> attempts before failing read procedure</td>
</tr>
<tr>
<td>OPENCV_FFMPEG_DECODE_ATTEMPTS</td>
<td>num</td>
<td>64</td>
<td>number of failed <code>avcodec_receive_frame</code> attempts before failing decoding procedure</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_GSTREAMER_CALL_DEINIT</td>
<td>bool</td>
<td>false</td>
<td>close GStreamer instance on end</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_GSTREAMER_START_MAINLOOP</td>
<td>bool</td>
<td rowspan="2">false</td>
<td>start GStreamer loop in separate thread</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_MFX_IMPL</td>
<td>num</td>
<td>set specific MFX implementation (see MFX docs for enumeration)</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_MFX_EXTRA_SURFACE_NUM</td>
<td>num</td>
<td>1</td>
<td>add extra surfaces to the surface pool</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_MFX_POOL_TIMEOUT</td>
<td>num</td>
<td>1</td>
<td>timeout for waiting for free surface from the pool (in seconds)</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_MFX_BITRATE_DIVISOR</td>
<td>num</td>
<td>300</td>
<td>this option allows to tune encoding bitrate (video quality/size)</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_MFX_WRITER_TIMEOUT</td>
<td>num</td>
<td>1</td>
<td>timeout for encoding operation (in seconds)</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS</td>
<td>bool</td>
<td>true</td>
<td>allow HW-accelerated transformations (DXVA) in MediaFoundation processing graph (may slow down camera probing process)</td>
</tr>
<tr>
<td>OPENCV_DSHOW_DEBUG</td>
<td>bool</td>
<td rowspan="2">false</td>
<td>enable verbose logging in the DShow backend</td>
</tr>
<tr>
<td>OPENCV_DSHOW_SAVEGRAPH_FILENAME</td>
<td>file path</td>
<td>enable processing graph tump in the DShow backend</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_V4L_RANGE_NORMALIZED</td>
<td>bool</td>
<td>false</td>
<td>use (0, 1) range for properties (V4L)</td>
</tr>
<tr>
<td>OPENCV_VIDEOIO_V4L_SELECT_TIMEOUT</td>
<td>num</td>
<td>10</td>
<td>timeout for select call (in seconds) (V4L)</td>
</tr>
<tr>
<td>OPENCV_VIDEOCAPTURE_DEBUG</td>
<td>bool</td>
<td>false</td>
<td>enable debug messages for VideoCapture</td>
</tr>
<tr>
<td>OPENCV_VIDEOWRITER_DEBUG</td>
<td>bool</td>
<td>false</td>
<td>enable debug messages for VideoWriter</td>
</tr>
<tr>
<td>⭐ OPENCV_VIDEOIO_DEBUG</td>
<td>bool</td>
<td>false</td>
<td>debug messages for both VideoCapture and VideoWriter</td>
</tr>
</tbody></table></div>
```

#### videoio tests

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td>OPENCV_TEST_VIDEOIO_BACKEND_REQUIRE_FFMPEG</td>
<td>bool</td>
<td rowspan="4">false</td>
<td>test app will exit if no FFmpeg backend is available</td>
</tr>
<tr>
<td>OPENCV_TEST_V4L2_VIVID_DEVICE</td>
<td>file path</td>
<td>path to VIVID virtual camera device for V4L2 test (e.g. <code>/dev/video5</code>)</td>
</tr>
<tr>
<td>OPENCV_TEST_PERF_CAMERA_LIST</td>
<td>paths</td>
<td>cameras to use in performance test (waitAny_V4L test)</td>
</tr>
<tr>
<td>OPENCV_TEST_CAMERA_%d_FPS</td>
<td>num</td>
<td>fps to set for N-th camera (0-based index) (waitAny_V4L test)</td>
</tr>
</tbody></table></div>
```

### highgui

```{raw} html
<div class="pst-scrollable-table-container"><table class="table opencv-rowspan-table">
<thead><tr>
<th>name</th>
<th>type</th>
<th>default</th>
<th>description</th>
</tr></thead>
<tbody>
<tr>
<td colspan="3">$XDG_RUNTIME_DIR</td>
<td>Wayland backend specific - create shared memory-mapped file for interprocess communication (named <code>opencv-shared-??????</code>)</td>
</tr>
<tr>
<td>OPENCV_HIGHGUI_FB_MODE</td>
<td>string</td>
<td rowspan="2"><code>FB</code></td>
<td>Selects output mode for the framebuffer backend (<code>FB</code> - regular frambuffer, <code>EMU</code> - emulation, perform internal checks but does nothing, <code>XVFB</code> - compatible with <em>xvfb</em> virtual frambuffer)</td>
</tr>
<tr>
<td>OPENCV_HIGHGUI_FB_DEVICE</td>
<td>file path</td>
<td>Path to frambuffer device to use (will be checked first)</td>
</tr>
<tr>
<td>FRAMEBUFFER</td>
<td>file path</td>
<td><code>/dev/fb0</code></td>
<td>Same as OPENCV_HIGHGUI_FB_DEVICE, commonly used variable for the same purpose (will be checked second)</td>
</tr>
</tbody></table></div>
```

### imgproc
| name | type | default | description |
|------|------|---------|-------------|
| OPENCV_OPENCL_IMGPROC_MORPH_SPECIAL_KERNEL | bool | true (Apple), false (others) | use special OpenCL kernel for small morph kernel (Intel devices) |
| OPENCV_GAUSSIANBLUR_CHECK_BITEXACT_KERNELS | bool | false | validate Gaussian kernels before running (src is [CV_16U](https://docs.opencv.org/5.x/d1/d1b/group__core__hal__interface.html#gaf55ae5a94c48cae66b96979877576f12), bit-exact version) |

### imgcodecs
| name | type | default | description |
|------|------|---------|-------------|
| OPENCV_IMGCODECS_AVIF_MAX_FILE_SIZE | num | 64MB | limit input AVIF size |
| OPENCV_IMGCODECS_WEBP_MAX_FILE_SIZE | num | 64MB | limit input WEBM size |
| OPENCV_IO_MAX_IMAGE_PARAMS | num | 50 | limit maximum allowed number of parameters in imwrite and imencode |
| OPENCV_IO_MAX_IMAGE_WIDTH | num | 1 << 20, limit input image size to avoid large memory allocations | |
| OPENCV_IO_MAX_IMAGE_HEIGHT | num | 1 << 20 | |
| OPENCV_IO_MAX_IMAGE_PIXELS | num | 1 << 30 | |
| OPENCV_IO_ENABLE_JASPER | bool | true (set build option OPENCV_IO_FORCE_JASPER), false (otherwise) | enable Jasper backend |

:::{note}
OPENCV_IO_ENABLE_OPENEXR is deprecated because bundled OpenEXR library had been removed.
:::
