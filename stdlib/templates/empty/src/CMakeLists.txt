cmake_minimum_required(VERSION 3.16)
project(mygame C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- Find libengine.a -------------------------------------------------------
# Adjust ENGINE_ROOT to wherever you built or installed the engine.
# In a typical dev layout the engine lives one level up; set LIBENGINE_DIR to
# override, e.g.  cmake -DLIBENGINE_DIR=/path/to/shooter/build ..
if(NOT DEFINED LIBENGINE_DIR)
    set(LIBENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../build"
        CACHE PATH "Directory containing libengine.a and its include headers")
endif()

find_library(ENGINE_LIB engine PATHS "${LIBENGINE_DIR}" NO_DEFAULT_PATH REQUIRED)
find_path   (ENGINE_INCLUDE app.h
    PATHS "${LIBENGINE_DIR}/../src/engine"
          "${LIBENGINE_DIR}/../../src/engine"
    NO_DEFAULT_PATH REQUIRED)

# ---- Game executable --------------------------------------------------------
add_executable(${PROJECT_NAME} main.c)
target_include_directories(${PROJECT_NAME} PRIVATE ${ENGINE_INCLUDE})
target_link_libraries(${PROJECT_NAME} PRIVATE ${ENGINE_LIB})
