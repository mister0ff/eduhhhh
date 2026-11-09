#!/bin/sh
# Compila e roda os testes de logica no PC (g++ ou clang++), sem Android.
set -e
cd "$(dirname "$0")/.."
${CXX:-g++} -std=c++20 -O1 -g -Wall -Wextra -Iinclude tests/test_logic.cpp \
  src/Templates.cpp src/Planner.cpp src/Executor.cpp src/Settings.cpp src/Render.cpp src/Controller.cpp \
  -o /tmp/autominer_tests
/tmp/autominer_tests
