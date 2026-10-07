<p align="right">
  <a href="sonic-web-companion.md">English</a> · <strong>简体中文</strong>
</p>

# Sonic Link Web Companion

Phase 9 在 `web-companion/` 增加静态移动端 PWA。它没有后端、账户、遥测、
分析、远程字体、CDN 或上传路径。浏览器与 AI Passport 使用现有 v1 二进制
40 字节帧通信。

## 本地构建与检查

需要 Node.js 20 或更高版本、npm，以及可从 `PATH` 使用的 Emscripten SDK
（也可设置 `EMCC` 和 `EMXX`）。在 `web-companion/` 中运行：

```sh
npm ci
npm run typecheck
npm run build
npm test
npm run test:wasm
npm run test:browser
```

`npm run build` 会将生产 `components/sonic_core/sonic_core.c` 与固定版本的
原版 ggwave donor 编译为本地 WASM，然后构建 PWA 和带版本号的 Service Worker
预缓存清单。构建结果放在忽略跟踪的 `dist/`，生成的 WASM 放在忽略跟踪的
`public/wasm/`。浏览器测试使用本地生产预览，需要对应版本的 Playwright
Chromium 浏览器。

指定 Emscripten 路径：

```sh
EMCC=/path/to/emsdk/upstream/emscripten/emcc \
EMXX=/path/to/emsdk/upstream/emscripten/em++ npm run build
```

## 运行时架构

`src/wasm/sonic-core-wrapper.c` 的 C ABI 调用生产 Sonic Core，负责载荷验证、
帧编码和接收重组。`src/wasm/ggwave-web-wrapper.cpp` 的 C++ ABI 使用共享的
AUDIBLE_FASTEST candidate 配置调用固定 donor。帧始终为精确的 40 字节，RX
保留二进制内容。

只有用户显式开始 Receive 后才会请求麦克风。浏览器请求关闭回声消除、自动
增益和噪声抑制。AudioWorklet 将浏览器任意大小的渲染量转换为有界 PCM 块；
Worker 负责对齐 donor 解码块、持续解码和 Sonic Core 重组。实际 AudioContext
实际 AudioContext 输入采样率会传给固定 donor 的重采样器；转换成 48 kHz 后再
送入 modem receiver。界面分别显示 AudioContext 与麦克风轨道采样率。Worklet
从真实麦克风样本计算限频 RMS 电平，用于接收信号条；发送页只显示真实帧和播放
进度。常规接收只在本机处理音频，不保留 PCM。

发送进度根据每个实际播放的 AudioBufferSource 完成事件更新，帧之间使用协议
间隔。只有最后一个音源播放结束后才显示完成。取消或页面进入后台都会显示为
中断。传输完成不代表设备已确认接收。

可选调试录音默认关闭、只应用于下一次接收，按 AudioContext 实际采样率最多
缓存 30 秒，仅保存在内存中，并且只有用户主动操作才下载 WAV。录音不会放入
Service Worker 缓存。

Web TEXT 会先把 CRLF 和 CR 规范为 LF，只接受可打印 ASCII 与 LF。TOKEN 接受
0 至 93 字节的任意二进制内容。每次构建会从生产资源派生 12 位构建 ID，并写入
`version.json` 与 Diagnostics，也作为 Service Worker 缓存名的一部分。
`npm run test:wasm` 使用真实 ggwave 检查 44.1/48 kHz 下的 40 字节帧恢复、任意
解码边界、前后静音、增益变化，以及削波/噪声/损坏区域下的有界退化。状态机覆盖
位于 `test_web_state`。

## 发布状态

当前配置仍标记为 **AUDIBLE_FASTEST candidate**，界面没有将其确定为出货配置。
Phase 9 不部署 PWA，也不把 Web 构建与固件版本配对。Safari/Chrome 实机声学
验收和 Passport 硬件 Gate 在指定设备上实际执行前都保持 NOT RUN。
