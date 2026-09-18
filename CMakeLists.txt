cmake_minimum_required(VERSION 3.22)

project(obs-depth-bokeh VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# libobs
#
# If you are building inside the obs-plugintemplate this is found for you.
# Standalone, point OBS_SOURCE_DIR at a cloned obs-studio checkout, or set
# libobs_DIR to an installed OBS SDK.
# ---------------------------------------------------------------------------
find_package(libobs REQUIRED)

# ---------------------------------------------------------------------------
# ONNX Runtime
#
# Set ONNXRUNTIME_ROOT to the extracted onnxruntime release directory, e.g.
#   -DONNXRUNTIME_ROOT=C:/dev/onnxruntime-win-x64-1.18.0
# ---------------------------------------------------------------------------
if(NOT DEFINED ONNXRUNTIME_ROOT)
  message(FATAL_ERROR "Set -DONNXRUNTIME_ROOT=<path to onnxruntime release>")
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  src/plugin-main.cpp
  src/depth-bokeh-filter.cpp
  src/depth-engine.cpp
  src/depth-engine.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  "${ONNXRUNTIME_ROOT}/include"
)

if(WIN32)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    OBS::libobs
    "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
  )
  # DirectML provider header lives next to the main headers in the
  # Microsoft.ML.OnnxRuntime.DirectML nuget package.
  if(EXISTS "${ONNXRUNTIME_ROOT}/include/dml_provider_factory.h")
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE USE_DML)
  endif()
elseif(APPLE)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    OBS::libobs
    "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib"
  )
else()
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    OBS::libobs
    "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.so"
  )
endif()

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES PREFIX "")

# Copy runtime data next to the built module so it can be dropped into the
# OBS plugin folders directly.
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/data"
)
