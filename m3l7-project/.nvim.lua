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

