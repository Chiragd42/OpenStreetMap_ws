cmake_minimum_required(VERSION 3.16)

project(osm_sheet1 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(EXPAT REQUIRED)
find_package(Threads REQUIRED)

find_path(OSMIUM_INCLUDE_DIR osmium/io/any_input.hpp
    HINTS /opt/homebrew/include /usr/local/include /usr/include)
find_path(PROTOZERO_INCLUDE_DIR protozero/pbf_reader.hpp
    HINTS /opt/homebrew/include /usr/local/include /usr/include)

if(NOT OSMIUM_INCLUDE_DIR)
    message(FATAL_ERROR "libosmium headers not found")
endif()
if(NOT PROTOZERO_INCLUDE_DIR)
    message(FATAL_ERROR "protozero headers not found")
endif()

add_executable(osm_inspect
    src/main.cpp
    src/osm_inspector.cpp
)

add_library(osm_sheet1_core
    src/basic_test.cpp
    src/http_server.cpp
    src/osm_extractor.cpp
    src/osm_model.cpp
    src/viewport_query.cpp
)

add_executable(osm_sheet1
    src/sheet1_main.cpp
)

target_include_directories(osm_inspect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(osm_sheet1_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(osm_inspect SYSTEM PRIVATE
    ${OSMIUM_INCLUDE_DIR}
    ${PROTOZERO_INCLUDE_DIR}
)

target_include_directories(osm_sheet1_core SYSTEM PRIVATE
    ${OSMIUM_INCLUDE_DIR}
    ${PROTOZERO_INCLUDE_DIR}
)

target_link_libraries(osm_inspect PRIVATE
    ZLIB::ZLIB
    BZip2::BZip2
    EXPAT::EXPAT
    Threads::Threads
)

target_link_libraries(osm_sheet1_core PRIVATE
    ZLIB::ZLIB
    BZip2::BZip2
    EXPAT::EXPAT
    Threads::Threads
)

target_link_libraries(osm_sheet1 PRIVATE osm_sheet1_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(osm_inspect PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_compile_options(osm_sheet1_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_compile_options(osm_sheet1 PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
