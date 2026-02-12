# SPDX-License-Identifier: MIT License
################################################################################
#
#  This document and its contents are parts of AGENT TEAM TEST project.
#  
#  Copyright (C) 2026 Tawan Thintawornkul <tawandawei@gmail.com>
#  
# 
#  @file Makefile
#  @ingroup dev_tooling
#  
#  @brief Makefile at project root directory for development convinience.
#
################################################################################

.PHONY: all clean build run test_node_1 test_node_2

PROJECT_ABSOLUTE_PATH := $(shell pwd)
PROJECT_BUILD_DIRECTORY := build
PROJECT_SCRIPT_DIRECTORY := script
PROJECT_CONFIG_DIRECTORY_NAME := config
PROJECT_EXEC_NAME := agent_team_test
PROJECT_CONFIG_FILENAME := project_config.json

all:
	@echo "Rules:\tclean, build, run, test_node_1, test_node_2"
	@echo "\tclean: clean all files in build/ directory."
	@echo "\tbuild: build project via cmake, in build/ directory."
	@echo "\trun: run built project binary, located in build/ directory."
	@echo "\ttest_node_1: run node 1 (src 127.0.0.1:5000 -> dst 127.0.0.1:6000)"
	@echo "\ttest_node_2: run node 2 (src 127.0.0.1:6000 -> dst 127.0.0.1:5000)"

clean:
	@rm -rf ${PROJECT_BUILD_DIRECTORY}/*

build:
	@mkdir -p ${PROJECT_BUILD_DIRECTORY}
	@mkdir -p ${PROJECT_BUILD_DIRECTORY}/${PROJECT_CONFIG_DIRECTORY_NAME}
	@cd ${PROJECT_BUILD_DIRECTORY} && cmake ../ && make
	@ln -sf ${PROJECT_ABSOLUTE_PATH}/${PROJECT_CONFIG_DIRECTORY_NAME}/${PROJECT_CONFIG_FILENAME} \
		${PROJECT_ABSOLUTE_PATH}/${PROJECT_BUILD_DIRECTORY}/${PROJECT_CONFIG_DIRECTORY_NAME}/${PROJECT_CONFIG_FILENAME}

run:
	@sudo -E ./${PROJECT_BUILD_DIRECTORY}/${PROJECT_EXEC_NAME}

test_node_1:
	@sudo -E ./${PROJECT_BUILD_DIRECTORY}/${PROJECT_EXEC_NAME} --src 127.0.0.1:5000 --dst 127.0.0.1:6000

test_node_2:
	@sudo -E ./${PROJECT_BUILD_DIRECTORY}/${PROJECT_EXEC_NAME} --src 127.0.0.1:6000 --dst 127.0.0.1:5000
