// Replacement for the CMake-generated config.h of Nuked-SC55 0.7.0.
#pragma once
#define NUKED_ENABLE_ASIO 0
#define NUKED_ENABLE_DECODER2 0
#define NUKED_VERSION "0.7.0"
#define NUKED_SOURCE "VSC-55-DOS / Nuked-SC55 0.7.0 / upstream 02f6e3d7bad89af33514bd48211bb950f8ad0e6b"
#define NUKED_VERSION_MAJOR 0
#define NUKED_VERSION_MINOR 7
#define NUKED_VERSION_PATCH 0
#include <cstdio>
void Cfg_WriteVersionInfo(FILE* file);
