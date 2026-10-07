<p align="right">
  <strong>简体中文</strong> · <a href="sonic-ggwave-profile.md">English</a>
</p>

# Sonic Link 固定 ggwave 配置

## 范围与版本固定

本主机/源码验证使用未修改的 `ggerganov/ggwave` donor，提交版本为
`060aec73dd7123ccac200442f75bdc7369795ffe`。本阶段不会选择最终声学配置，
也不能证明 Passport 运行时内存或声学性能。

产品侧配置构造器位于
`components/sonic_ggwave_profile/sonic_ggwave_profile.cpp`。它保留 donor
默认值，并设置 Sonic Link 对 Passport 的固定要求：

| 参数 | 值 |
|---|---:|
| Payload 长度 | 固定 40 字节 |
| 输入/输出/运行采样率 | 24,000 Hz |
| 每帧采样数 | 512 |
| 输入/输出采样格式 | I16 |
| 工作模式 | RX + TX + TX_ONLY_TONES + DSS |

每次准备实例前，配置器会重置 donor 的 RX、TX 全局协议集合，并且每个
方向只启用当前候选配置。两个候选分别独立测试：

| 候选配置 | RX 与 TX 协议 | Stock 主机堆内存 |
|---|---|---:|
| AUDIBLE_FASTEST | `GGWAVE_PROTOCOL_AUDIBLE_FASTEST` | 99,824 字节 |
| AUDIBLE_FAST | `GGWAVE_PROTOCOL_AUDIBLE_FAST` | 185,840 字节 |

这些固定值来自 pinned donor 的
`GGWave::prepare(parameters, false)` 计算结果。测试还会真实分配一个 stock
实例，并确认 `heapSize()` 与试算值相同。两种配置的每次发送帧数不同，会改变
固定接收频谱历史和 tone-plan 缓冲区大小，因此内存结果不同。

## 分配模型

donor 在 `ggwave_vendor/src/ggwave.cpp` 的 `GGWave::alloc` 中计算上述内存，包含：

- 编码 payload 与 ECC 数据；
- 固定 RX FFT、输入、解码数据、检测 bin/tone 缓冲区；
- 按 `totalTxs * maxFramesPerTx * samplesPerFrame` 分配的 `spectrumHistoryFixed`；
- TX 数据、bit 与 tone-plan 缓冲区；
- Reed-Solomon 工作数据。

固定 RX 历史使用 donor 的 `totalTxs` 计算；相关辅助函数对当前协议每次发送的字节
数估算方式可能造成历史区过度分配。本阶段只记录 stock 行为，不修改 allocator。
没有启用变长 RX；TX_ONLY_TONES 避免分配 donor 的完整波形 TX 缓冲区。

以上字节数是 pinned stock donor 的主机/源码证据，不代表 Passport 的剩余堆内存或
最大连续内存块。G18 必须在真实 Passport 上测量，结果具有最终决定权；只有当测量
证明满足规范中的内存失败条件时，才考虑仅调整 allocator 大小。

## 验证

`tests/test_ggwave_profile.cpp` 检查参数约束、候选映射、RX/TX 协议隔离、DSS 与实例
属性、试算和实际分配内存一致性、固定版本内存回归值，以及从 FASTEST 切换到 FAST
时旧协议会被关闭。该测试由 `./tools/validate.sh --static` 运行。
