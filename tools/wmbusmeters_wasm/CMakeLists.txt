cmake_minimum_required(VERSION 3.28)
project(wmbusmeters_wasm C CXX)

execute_process(COMMAND git describe --tags --always --dirty OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
set(ENV{ESPHOME_DATA_DIR} ${CMAKE_BINARY_DIR}/esphome)
execute_process(COMMAND esphome -s version ${VERSION} compile --only-generate wmbusmeters_wasm.yaml
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND_ERROR_IS_FATAL ANY)

include(FetchContent)
FetchContent_Declare(ArduinoJson EXCLUDE_FROM_ALL
  URL https://github.com/bblanchon/ArduinoJson/archive/refs/tags/v$ENV{ARDUINOJSON_VERSION}.tar.gz)
FetchContent_Declare(MbedTLS EXCLUDE_FROM_ALL
  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$ENV{MBEDTLS_VERSION}/mbedtls-$ENV{MBEDTLS_VERSION}.tar.bz2)
FetchContent_MakeAvailable(ArduinoJson MbedTLS)

add_compile_options(-Oz -flto -fno-exceptions -DNDEBUG)
set(GENERATED ${CMAKE_BINARY_DIR}/esphome/build/wmbusmeters-wasm)
file(GLOB_RECURSE SOURCES ${GENERATED}/src/esphome/components/wmbus_common/*.cpp ${GENERATED}/src/wmbusmeters_generated/*.cpp)
# component.cpp is the ESPHome glue the decoder does without.
list(FILTER SOURCES EXCLUDE REGEX "/component\\.cpp$")
# USE_HOST on every source would switch crypto.cpp to OpenSSL.
set(CORE_HELPERS ${GENERATED}/src/esphome/core/helpers.cpp ${GENERATED}/src/esphome/core/alloc_helpers.cpp)
set_source_files_properties(${CORE_HELPERS} PROPERTIES COMPILE_DEFINITIONS USE_HOST)

add_executable(wmbusmeters_wasm wmbusmeters_wasm.cpp ${SOURCES} ${CORE_HELPERS})
target_compile_features(wmbusmeters_wasm PRIVATE cxx_std_20)
target_include_directories(wmbusmeters_wasm PRIVATE ${GENERATED}/src)
target_link_libraries(wmbusmeters_wasm PRIVATE ArduinoJson mbedcrypto)
target_link_options(wmbusmeters_wasm PRIVATE -Oz -flto
  -sMODULARIZE -sEXPORT_ES6 -sSINGLE_FILE -sENVIRONMENT=web,node -sALLOW_MEMORY_GROWTH -sFILESYSTEM=0
  -lembind --extern-pre-js=${GENERATED}/banner.js --pre-js=${GENERATED}/metadata.js)
set_target_properties(wmbusmeters_wasm PROPERTIES
  LINK_DEPENDS "${GENERATED}/banner.js;${GENERATED}/metadata.js")
