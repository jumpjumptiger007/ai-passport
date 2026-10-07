<p align="right">
  <a href="sonic-release-candidate.md">English</a> · <strong>简体中文</strong>
</p>

# Sonic Link 发布候选版本

Phase 10 生成可复现的候选配对版本。`AUDIBLE_FASTEST` 在 G22 选出发货配置前
仍是候选配置；扬声器音量在 G21 完成校准前仍未校准。

## 生成本地候选证据

按项目脚本构建并验证 `web-companion/`，再使用 ESP-IDF 5.5.3 运行仓库官方固件
验证流程。该流程会将固件归档保存在 `build/firmware/`。

在仓库根目录运行：

```sh
python3 tools/sonic-release-candidate.py
python3 tools/check-sonic-release-pair.py --manifest build/sonic-release/sonic-link-candidate.json
```

生成器会记录源码版本与脏状态、经验证的固件归档哈希、Web 版本与缓存身份、每个
生产 Web 文件的 SHA-256 清单，以及当前 Demo URL。它不会执行部署。真实部署前，
元数据会明确标记固件为部署前候选；占位 URL 不能视为已配对发布版本。

## 部署与 URL 配对

只能将已验证的 `web-companion/dist/` 部署到明确选定且已授权的静态 HTTPS 站点。
需要将站点的 `version.json`、worker、worklet、WASM 文件、manifest、service worker、
缓存身份和浏览器冒烟结果与本地构建逐项核对。之后将唯一的 `SONIC_DEMO_URL` Kconfig
默认值设置为该已验证 HTTPS URL，在 ESP-IDF 5.5.3 下运行
`./tools/validate.sh --firmware` 重新构建，再使用已验证的部署 URL 重新生成元数据。
若 URL 与固件配置不一致，生成器会报错。

仓库不会默认指定托管服务商、项目、域名、DNS 所有权或凭据。目标与访问权限明确前
不得部署。

## 状态用语

候选元数据不是最终发布报告。未实际运行的硬件与物理声学 Gate 必须保持 `NOT RUN`。
软件候选完成后只能报告 `IMPLEMENTATION COMPLETE / HARDWARE UNVERIFIED`；不能报告
`DEMO COMPLETE`，也不能声称已选定发货声学配置或音量。
