cmake_minimum_required(VERSION 3.16)

project(osm_geocoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

set(GEOCODER_CORE_SOURCES
    src/app.cpp
    src/model.cpp
    src/metrics.cpp
    src/ingest/gpkg_extractor.cpp
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
)

target_link_libraries(geocoder_core
    PRIVATE
        SQLite::SQLite3
)

add_executable(osm_geocoder
    src/main.cpp
)

target_link_libraries(osm_geocoder
    PRIVATE
        geocoder_core
)
