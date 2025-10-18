cmake_minimum_required(VERSION 3.15)

project(DataContainer-Lua)
set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND DC_PARSER "src/parser/parsing.cpp")
list(APPEND DCL_CORE "src/LuaFFIGenerator.cpp")

add_executable(
        DataContainer-Lua
	${DC_PARSER}
	${DCL_CORE}
)
target_compile_definitions(DataContainer-Lua PRIVATE INCREMENTAL=1)
target_include_directories(DataContainer-Lua PRIVATE ${PROJECT_SOURCE_DIR}/src/parser)

if (MSVC)
	target_compile_options(DataContainer-Lua PRIVATE /W4)
else ()
	target_compile_options(DataContainer-Lua PRIVATE -Wall -Wextra -Wpedantic)
endif ()
