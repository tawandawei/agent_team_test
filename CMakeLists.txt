# SPDX-License-Identifier: MIT License
################################################################################
#
# This document and its contents are parts of the AGENT TEAM TEST project.
# 
# Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
# 
#
# @file CMakeLists.txt
# @ingroup main
# 
# @brief CMakeLists.txt for agent team test project
#
################################################################################
cmake_minimum_required(VERSION 3.20)


################################################################################
# PROJECT CONFIGURATION
################################################################################
set(PROJECT_NAME_BASE agent_team_test)


set(PROJECT_VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)


################################################################################
# PROJECT SETUP
################################################################################
project(${PROJECT_NAME_BASE} VERSION ${PROJECT_VERSION})


################################################################################
# PACKAGES
################################################################################
set(PROJECT_LIBS
    
)

################################################################################
# PROJECT RESOURCES
################################################################################
set(SRCS_APP
    src/app/main.cpp
    src/app/ArgParser.cpp
    src/app/AppPacket.cpp
    src/app/SignalHandler.cpp
)

set(SRCS_EVENT
    src/event/EventLoop.cpp
)

set(SRCS_SOCKET
    src/socket/UdpNode.cpp
)

set(SRCS_TIMER
    src/timer/Timer.cpp
)

set(SRCS_THREAD
    src/thread/UdpThreadManager.cpp
)

set(SRCS_INCLUDE_PATHS
    include/
)

set(SRCS_ALL_MODULES
    ${SRCS_APP}
    ${SRCS_EVENT}
    ${SRCS_SOCKET}
    ${SRCS_TIMER}
    ${SRCS_THREAD}
)


################################################################################
# TARGET BUILDING : BASE
################################################################################
add_executable(${PROJECT_NAME_BASE}
    ${SRCS_ALL_MODULES}    
)
target_link_libraries(${PROJECT_NAME_BASE} PRIVATE
    ${PROJECT_LIBS}
)
target_include_directories(
    ${PROJECT_NAME_BASE} PRIVATE
    ${SRCS_INCLUDE_PATHS}
)
target_compile_definitions(
    ${PROJECT_NAME_BASE} PRIVATE
    SYSTEM_SIDE=${SYSTEM_SIDE_A}  # A Side
)
set_target_properties(${PROJECT_NAME_BASE} PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME_BASE}
)

################################################################################
# INSTALLATION
################################################################################
install(TARGETS ${PROJECT_NAME_BASE}
    RUNTIME DESTINATION bin
)
