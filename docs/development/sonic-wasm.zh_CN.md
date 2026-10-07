<p align="right">
  <strong>简体中文</strong> · <a href="sonic-wasm.md">English</a>
</p>

# Sonic Link WASM 验证

该验证会将生产使用的 `sonic_core.c` 分别编译为 native 和 WebAssembly，
然后逐字节比较 canonical transcript。脚本也会把固定版本的原版 ggwave
编译到 WebAssembly，并在 Node.js 中运行完整的 40 字节二进制编码/解码。
声波往返使用 donor 原始实现，并验证嵌入 NUL 后的字节仍然完整。

## 运行

可将 Emscripten SDK 安装或激活到用户选择的临时目录。验证脚本不会修改项目
源码或机器级 shell 配置。将 `EM_CONFIG` 指向 SDK 内的 `.emscripten`，并在
编译器不在 `PATH` 中时指定其路径：

```sh
EM_CONFIG=/path/to/emsdk/.emscripten \
EMCC=/path/to/emsdk/upstream/emscripten/emcc \
EMXX=/path/to/emsdk/upstream/emscripten/em++ \
NODE=/path/to/node \
./tools/validate-wasm.sh
```

可用 `CC` 和 `CXX` 选择 native 编译器。构建产物和 transcript 保存在临时
目录中，命令结束时会清理。只有 native 与 WASM transcript 完全一致时，G05
才通过；只有 stock ggwave WASM 收发器逐字节恢复完整的 40 字节（包括嵌入
NUL 和高位字节）时，G06 才通过。

使用 `./tools/validate.sh --static` 运行完整 host/static 仓库验证。WASM 构建
需要单独显式运行，不能用 native-only 结果代替。Passport 实机声学验证属于
硬件 gate，不包含在本机 WASM 验证中。
