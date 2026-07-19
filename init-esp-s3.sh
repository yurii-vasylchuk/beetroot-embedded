#!/bin/bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <project dir>"
  exit 1
fi

PROJECT_DIR="$1"

mkdir "$PROJECT_DIR"

cd "$PROJECT_DIR"

pio init -b esp32-s3-devkitc-1

touch .clangd

cat >./.clangd <<EOF
CompileFlags:
  Remove:
    - -fno-shrink-wrap
    - -fno-tree-switch-conversion
    - -fstrict-volatile-bitfields
    - -mlongcalls

EOF

cat >./.nvim.lua <<EOF
local toolchain = vim.fn.expand("~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-*")

vim.lsp.config("clangd", {
        cmd = {
                "clangd",
                "--background-index",
                "--clang-tidy",
                "--header-insertion=never",
                "--query-driver=" .. toolchain,
        },
})

EOF

cat >>platformio.ini <<EOF

targets = compiledb

EOF

pio run
