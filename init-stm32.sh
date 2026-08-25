#!/bin/bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <project dir>"
  exit 1
fi

PROJECT_DIR="$1"

cd "$PROJECT_DIR"

cat >>CMakeLists.txt <<'EOF'

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(
    STM32_PROGRAMMER_CLI
    NAMES STM32_Programmer_CLI STM32_Programmer_CLI.exe
)

if(STM32_PROGRAMMER_CLI)

  add_custom_target(flash
        COMMAND
            ${STM32_PROGRAMMER_CLI}
            -c port=SWD
            -w $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            -v
            -rst

        DEPENDS ${CMAKE_PROJECT_NAME}

        COMMENT "Flashing M3L0 via ST-Link/SWD"
        VERBATIM
    )
else()
  message(WARNING "STM32_Programmer_CLI not found")
endif()

EOF

cat >./.gitignore << 'EOF'

# -----------------------------
# Build directories
# -----------------------------
build/
cmake-build-*/
out/

# -----------------------------
# CMake generated files
# -----------------------------
CMakeCache.txt
CMakeFiles/
cmake_install.cmake
Makefile
build.ninja
.ninja_deps
.ninja_log

# -----------------------------
# Firmware artifacts
# -----------------------------
*.elf
*.hex
*.bin
*.map
*.list
*.lst

# -----------------------------
# Compiler generated
# -----------------------------
*.o
*.obj
*.d
*.su
*.a

# -----------------------------
# compile_commands
# -----------------------------
compile_commands.json

# -----------------------------
# IDE / editor
# -----------------------------
.vscode/
.idea/
*.code-workspace

# macOS
.DS_Store

# -----------------------------
# STM32CubeIDE
# -----------------------------
.settings/
.metadata/
Debug/
Release/

# -----------------------------
# Logs / temporary files
# -----------------------------
*.log
*.tmp
*.bak
*~

EOF

cmake --preset Debug
cmake --build --preset Debug
cp ./build/Debug/compile_commands.json ./compile_commands.json
