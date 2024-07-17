cmake_minimum_required(VERSION 3.13.5 FATAL_ERROR)
project(warehouse-backend-example CXX)

# -- includes ------------------------------------------------------------------

include(FetchContent)

# -- opt-in to new behavior ----------------------------------------------------

# See https://cmake.org/cmake/help/latest/policy/CMP0135.html
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# -- options -------------------------------------------------------------------

option(BUILD_SHARED_LIBS "Build shared library targets" OFF)

set(SANITIZERS "" CACHE STRING "Selected sanitizers, e.g., 'address,undefined'")

set(CAF_TAG "1.0.0" CACHE STRING "The tag of the CAF version to use")

set(CXX_VERSION "17" CACHE STRING "The C++ version to use")

# -- get our dependencies ------------------------------------------------------

find_package(SQLite3 REQUIRED)

# -- embed CAF -----------------------------------------------------------------

function(get_caf)
  # Fetch CAF from the official repository.
  FetchContent_Declare(
    caf
    GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
    GIT_TAG        ${CAF_TAG}
  )
  FetchContent_Populate(caf)
  # Select the parts of CAF we need.
  set(CAF_ENABLE_NET_MODULE ON)
  set(CAF_ENABLE_EXAMPLES OFF)
  set(CAF_ENABLE_IO_MODULE OFF)
  set(CAF_ENABLE_TESTING OFF)
  set(CAF_ENABLE_TOOLS OFF)
  # Pass along build options.
  set(CAF_CXX_VERSION ${CXX_VERSION})
  set(CAF_SANITIZERS ${SANITIZERS})
  # Build CAF as a sub-project.
  add_subdirectory(${caf_SOURCE_DIR} ${caf_BINARY_DIR})
endfunction()
get_caf()

# -- build the example ---------------------------------------------------------

set(srcs warehouse-backend-example)

add_executable(warehouse-backend-example
  ${srcs}/controller_actor.cpp
  ${srcs}/database.cpp
  ${srcs}/database_actor.cpp
  ${srcs}/ec.cpp
  ${srcs}/http_server.cpp
  ${srcs}/main.cpp
)

target_link_libraries(warehouse-backend-example PRIVATE CAF::net SQLite::SQLite3)

target_compile_features(warehouse-backend-example PRIVATE cxx_std_${CXX_VERSION})
