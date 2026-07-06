# CMakeList.txt : CMake project for CrossPlatformPractice, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.23)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()
set(ENV{CMAKE_BUILD_PARALLEL_LEVEL} 8)

file(READ ${CMAKE_CURRENT_SOURCE_DIR}/Project This_Project)
project(${This_Project})

set(CMAKE_CXX_STANDARD 20)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libs_dep.json)
set(LIBS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/libs_dep.json LIBS_DEP_JSON)
string(JSON total_libs LENGTH "${LIBS_DEP_JSON}" libs)
MATH(EXPR total_libs "${total_libs}-1")
foreach(IDX RANGE ${total_libs})
    # Get the name from the current JSON element 'name'.
    string(JSON CUR_NAME GET ${LIBS_DEP_JSON} libs ${IDX} name)
    # Get the URL from the current JSON element 'prj_name'.
    string(JSON TARGET_LINK_PRJ GET ${LIBS_DEP_JSON} libs ${IDX} prj_name)
    if (NOT TARGET ${TARGET_LINK_PRJ})
       #set(${TARGET_LINK_PRJ}_PREFER_USE_PREBUILT ON)
       add_subdirectory(${LIBS_DIR}/${CUR_NAME} ${CMAKE_BINARY_DIR}/${CUR_NAME})
    endif()
    list(APPEND TARGETS_LINK_PRJ ${TARGET_LINK_PRJ})
endforeach()
endif()

# Add source to this project's executable.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS ${INCLUDE_DIR}/*.h ${INCLUDE_DIR}/*.hpp)
include_directories(${INCLUDE_DIR})

add_executable (${PROJECT_NAME} ${SOURCES})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_CURRENT_SOURCE_DIR}/Debug
                      RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_CURRENT_SOURCE_DIR}/Release
                      RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_CURRENT_SOURCE_DIR}/MinSizeRel
                      RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_CURRENT_SOURCE_DIR}/RelWithDebInfo
)
target_sources(${PROJECT_NAME} PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${INCLUDE_DIR}
    FILES ${HEADERS}
)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/assets)
add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/assets)
add_dependencies(${PROJECT_NAME} copy_assets)
# file(COPY assets DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/${CMAKE_BUILD_TYPE})
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets ${CMAKE_CURRENT_SOURCE_DIR}/$<CONFIG>/assets)
endif()
foreach(TARGET_LINK_LIB ${TARGETS_LINK_PRJ})
if (PREBUILT_${TARGET_LINK_LIB})
include_directories(${${TARGET_LINK_LIB}_INCLUDE})
set(TARGET_LINK_LIB ${PREBUILT_${TARGET_LINK_LIB}})
endif()
target_link_libraries(${PROJECT_NAME} ${TARGET_LINK_LIB})
endforeach()

target_precompile_headers(${PROJECT_NAME} PUBLIC ${INCLUDE_DIR}/ServerStdafx.h)