# AuraFlow ESP32 firmware — top-level ESP-IDF project.
# Build: idf.py -C src/firmware/c build
# Flash: idf.py -C src/firmware/c flash monitor

cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(auraflow)
