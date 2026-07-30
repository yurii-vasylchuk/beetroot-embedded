#!/bin/bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <project dir>"
  exit 1
fi

PROJECT_DIR="$1"

mkdir "$PROJECT_DIR"

cd "$PROJECT_DIR"

pio init --ide vim --sample-code --project-option "framework=espidf" -b esp32-s3-devkitc-1

touch .clangd

cat >./.clangd <<EOF
CompileFlags:
  Add:
    - -Wno-unknown-attributes
    - -Wno-attributes
  Remove:
    - -m*
    - -f*

Diagnostics:
  UnusedIncludes: None
  MissingIncludes: None

EOF
#:lua vim.cmd('edit ' .. vim.lsp.get_log_path()) <- check logs
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
        cmd_env = {
                PATH = "/Users/yurii/.platformio/packages/toolchain-xtensa-esp32s3/bin:" .. vim.env.PATH,
        },
})

EOF

cat >>platformio.ini <<EOF

monitor_speed = 115200

EOF

pio run -t compiledb
