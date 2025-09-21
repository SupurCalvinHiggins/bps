cmake_minimum_required(VERSION 3.16)
project(bps LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PREDICTOR_FLAGS "" CACHE STRING "Extra predictor flags")
separate_arguments(PREDICTOR_FLAGS)

if(NOT DEFINED PREDICTOR)
    message(FATAL_ERROR "Specify predictor via -DPREDICTOR=<name>")
endif()

string(TOUPPER "${PREDICTOR}" PREDICTOR_UPPER)

add_compile_definitions("BPS_USE_${PREDICTOR_UPPER}")

set(SOURCES
    simulator/main.cc
    simulator/predictor.cc
    simulator/tracer.cc
)

add_executable(bps ${SOURCES})

target_include_directories(bps PRIVATE
    simulator
    predictors
)

target_compile_options(bps PRIVATE
    -O3
    -Wall
    ${PREDICTOR_FLAGS}
)
