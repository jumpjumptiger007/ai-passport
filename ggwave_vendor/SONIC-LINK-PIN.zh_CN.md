<p align="right">
  <strong>简体中文</strong> · <a href="SONIC-LINK-PIN.md">English</a>
</p>

# Sonic Link ggwave donor pin

- 上游仓库：https://github.com/ggerganov/ggwave
- 提交：`060aec73dd7123ccac200442f75bdc7369795ffe`
- 内容：上游源文件、公开头文件、FFT 与 Reed-Solomon 依赖，以及上游许可证声明。
- 本阶段没有修改 donor 行为。
- 根目录 `CMakeLists.txt` 是为裁剪后的源码子集提供的 Sonic Link 构建包装；donor C++ 源文件仍与上游逐字节一致。

上游 `LICENSE` 与 `src/reed-solomon/LICENSE` 均保留。
