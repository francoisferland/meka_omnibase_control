cmake_minimum_required(VERSION 2.8)
project(meka_omnibase_control)

set(LIBNAME "meka_omnibase_control")

set(RTAI on)

if(BUILD_LOCAL)
    set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
endif(BUILD_LOCAL)

find_package(M3 COMPONENTS HARDWARE CHAINS TOOLBOX VEHICLES)
find_package(M3rt)
find_package(RTAI REQUIRED)
add_definitions(-D__RTAI__)
find_package(Boost REQUIRED)
find_package(Eigen3 3 REQUIRED)

## First generate the python 
find_package(Protobuf REQUIRED)
SET(PROTOBUF_IMPORT_DIRS ${PROTOBUF_IMPORT_DIRS} ${M3RT_INCLUDE_DIR} ${M3_INCLUDE_DIR})

file(GLOB ProtoFiles "${PROJECT_SOURCE_DIR}/src/*.proto")
# Python protobufs
find_package(PythonLibs 2.7 REQUIRED)
find_package(PythonInterp REQUIRED) 
# Generate and copy the python protobuf
PROTOBUF_GENERATE_PYTHON(PySources ${CMAKE_CURRENT_BINARY_DIR} ${ProtoFiles}) 
add_custom_target(${LIBNAME}_python_protobuf_output ALL DEPENDS ${PySources}) # force the generation
# End Protobuf stuff

## YAML CPP 
find_package(PkgConfig REQUIRED)
#find_package(YamlCpp  REQUIRED)
pkg_check_modules(YAMLCPP REQUIRED yaml-cpp)
if(${YAMLCPP_VERSION} VERSION_LESS "0.5")
set_property(
   DIRECTORY
   PROPERTY COMPILE_DEFINITIONS YAMLCPP_03
   )
endif(${YAMLCPP_VERSION} VERSION_LESS "0.5")
set(LIBS ${LIBS} ${YAMLCPP_LIBRARY})

if (RTAI)

include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}           # For Protobuf-generated files.
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/omni_kinematics/include
    ${M3RT_INCLUDE_DIR}
    ${RTAI_INCLUDE_DIR}
    ${THREADS_INCLUDE_DIR}
    ${EIGEN3_INCLUDE_DIR}
    ${PROTOBUF_INCLUDE_DIR}
    ${M3_INCLUDE_DIR}
    ${M3_SRC}
    # TODO: This is necessary for protobufs files including other generated
    # protobuf files without specifying the folder/module.
    # It should be part of the protobuf macros.
    ${CMAKE_CURRENT_BINARY_DIR}/../m3meka/src/m3/hardware
    ${CMAKE_CURRENT_BINARY_DIR}/../m3meka/src/m3/chains
    ${CMAKE_CURRENT_BINARY_DIR}/../m3meka/src/m3/toolbox
)

# Start Protobuf stuff
# Cpp protobufs
PROTOBUF_GENERATE_CPP(ProtoSources ProtoHeaders ${CMAKE_CURRENT_BINARY_DIR} ${ProtoFiles})
set_source_files_properties(${ProtoSources} ${ProtoHeaders} PROPERTIES
                            GENERATED TRUE)
add_custom_target(${LIBNAME}_protobuf_tmp  ALL DEPENDS ${ProtoSources}) # Force the re-generation

# OROCOS KDL
find_package(orocos_kdl REQUIRED)
include_directories(${orocos_kdl_INCLUDE_DIRS})
set(LIBS ${LIBS} ${orocos_kdl_LIBRARIES})

set(LIBS ${LIBS} omni_kinematics ${RTAI_LIBRARIES}  pthread ${Boost_LIBRARIES} ${EIGEN3_LIBRARIES} ${PROTOBUF_LIBRARIES} m3chains m3toolbox m3hardware m3base)

set( ALL_SRCS 
    src/caster_control.cpp
    src/meka_omnibase_control.cpp
    src/factory_proxy.cpp
)
set( ALL_HDRS
    include/meka_omnibase_control/meka_omnibase_control.hpp
)

list(APPEND ALL_SRCS ${ProtoSources})

list(APPEND ALL_HDRS
${ProtoHeaders}
${ProtoFiles}
)



## Building
set(CMAKE_CXX_FLAGS "-std=c++0x")

# This enables writing the state of the base directly to the server output:
add_definitions(-DDEBUG_OUTPUT=0)

add_subdirectory(omni_kinematics)
add_subdirectory(python)

add_library(${LIBNAME} SHARED ${ALL_SRCS})
target_link_libraries(${LIBNAME} ${LIBS})
set_target_properties(${LIBNAME} PROPERTIES 
                      INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib)
#set_target_properties(${LIBNAME} PROPERTIES COMPILE_FLAGS "-I${CMAKE_BINARY_DIR}/src")

## Installing
install(TARGETS ${LIBNAME} DESTINATION lib)
install(FILES ${ALL_HDRS} DESTINATION include/m3/${PROJECT_NAME})

endif(RTAI)

execute_process ( 
   COMMAND ${PYTHON_EXECUTABLE} -c 
   	"import site, sys; sys.stdout.write(site.PREFIXES[-1])" 
   OUTPUT_VARIABLE PYTHON_PREFIX 
) 
file ( TO_CMAKE_PATH "${PYTHON_PREFIX}" PYTHON_PREFIX ) 
execute_process ( 
   COMMAND ${PYTHON_EXECUTABLE} -c 
   	"import site, sys; sys.stdout.write(site.getsitepackages()[-1])" 
   OUTPUT_VARIABLE PYTHON_SITE_DIR 
) 
file ( TO_CMAKE_PATH "${PYTHON_SITE_DIR}" PYTHON_SITE_DIR ) 
string ( REGEX REPLACE "^${PYTHON_PREFIX}/" "" 
   PYTHON_SITE_DIR "${PYTHON_SITE_DIR}" 
) 


install ( FILES ${PySources} 
   DESTINATION ${PYTHON_SITE_DIR}/m3
   COMPONENT library 
) 
