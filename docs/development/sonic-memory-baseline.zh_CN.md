<p align="right">
  <strong>简体中文</strong> · <a href="sonic-memory-baseline.md">English</a>
</p>

# Sonic Link Phase 8 内存基线

## 决策

继续使用固定 40 字节帧和固定版本的 stock ggwave donor。由于尚未在真实
Passport 上观察到 stock 内存失败，因此没有修改 allocator。必须在实体
Passport 上测量下列阈值后才能将 G18 标记为通过；模拟器和主机数值仅供参考。

本次没有发现 Passport 串口设备。可用串口只有 macOS debug console 和
Bluetooth incoming port。现有 Passport Simulator 已从 Phase 8 镜像启动并进入
Sonic Link，但 UART 面板没有显示 `SONIC_MEM` 记录，因此不报告模拟器堆数值。

## 测量代码

音频引擎会保留当前 candidate、donor 所需堆、codec 初始化前立即采样的最大
空闲块、24 KiB 预留阈值，以及 preflight 是否通过。即使 codec 初始化失败，
这些数值也会保留。应用在取得第一份音频诊断快照后输出结构化 `SONIC_MEM` 行，
包括运行时空闲堆、最低空闲堆、最大块和 codec 堆。

主机阈值 evaluator 只计算数值，不判断测量来自实体设备，也不单独宣告 G18
通过。阈值如下：

- codec 初始化前最大空闲块：至少为 donor 所需堆 + 24,576 字节。
- 运行时空闲堆：至少 49,152 字节。
- 运行时最低空闲堆：至少 32,768 字节。
- 运行时最大空闲块：至少 24,576 字节。

当前 stock `AUDIBLE_FASTEST` candidate 的 donor 所需堆为 99,824 字节，因此
初始化前最大空闲块至少需要 124,400 字节。

## Phase 8 固件身份

- 目标：ESP32-C3；ESP-IDF：v5.5.3。
- 源码基线：`33d3d1d93a1125b356b47b6d83a7a60121be801e`，加上尚未提交的 Phase 8
  测量改动。
- Sonic 语义版本：1.0.0。
- 声学 candidate：`AUDIBLE_FASTEST`（当前候选，不代表最终发布选择）。
- Donor：固定版本的 stock ggwave 0.4.3；未修改 donor 源码。
- App 镜像：810,432 字节；合并完整镜像：875,968 字节。
- 完整镜像归档：`build/firmware/7a102ad21dbf3599887683459e571fd593ab11b70f7d360ae9202028dacceeed`。
- 完整镜像 SHA-256：`7a102ad21dbf3599887683459e571fd593ab11b70f7d360ae9202028dacceeed`。
- ELF SHA-256：`309b9f206ae4b1960cf6351017d1dde1bed1b4926e9777c23c971fa1d0114087`。
- DEVICE_INFO 指纹字节：`30 9B 9F 20`；显示为 `309B9F20`。

## 门禁状态

- 固件构建和布局校验：PASS。
- 主机内存阈值边界和 preflight 失败数据保留测试：PASS。
- G05/G06 WASM 回归：PASS。
- 现有模拟器启动到 Sonic Link：PASS。
- G18 实体内存门：NOT RUN；没有可用的真实 Passport。
- 没有修改 allocator，也没有减小帧长度。
