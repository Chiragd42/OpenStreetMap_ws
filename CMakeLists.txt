cmake_minimum_required(VERSION 3.16)

project(osm_geocoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)
find_package(BZip2 REQUIRED)
find_package(EXPAT REQUIRED)
find_package(Threads REQUIRED)

find_path(OSMIUM_INCLUDE_DIR osmium/io/any_input.hpp
    HINTS
        /opt/homebrew/include
        /usr/local/include
        /usr/include
)

if(NOT OSMIUM_INCLUDE_DIR)
    message(FATAL_ERROR "libosmium headers not found. Install libosmium (and protozero) first.")
endif()

find_path(PROTOZERO_INCLUDE_DIR protozero/pbf_reader.hpp
    HINTS
        /opt/homebrew/include
        /usr/local/include
        /usr/include
)

if(NOT PROTOZERO_INCLUDE_DIR)
    message(FATAL_ERROR "protozero headers not found. Install protozero first.")
endif()

set(GEOCODER_CORE_SOURCES
    src/app.cpp
    src/cache/datastore_cache.cpp
    src/model.cpp
    src/metrics.cpp
    src/ingest/pbf_extractor.cpp
    src/query/bbox_query.cpp
    src/query/json.cpp
    src/server/http_server.cpp
)

add_library(geocoder_core
    ${GEOCODER_CORE_SOURCES}
)

target_include_directories(geocoder_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${OSMIUM_INCLUDE_DIR}
        ${PROTOZERO_INCLUDE_DIR}
)

target_link_libraries(geocoder_core
    PRIVATE
        ZLIB::ZLIB
        BZip2::BZip2
        EXPAT::EXPAT
        Threads::Threads
)

add_executable(osm_geocoder
    src/main.cpp
)

target_link_libraries(osm_geocoder
    PRIVATE
        geocoder_core
)
