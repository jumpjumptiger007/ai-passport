<p align="right">
  <strong>English</strong> · <a href="SONIC-LINK-PIN.zh_CN.md">简体中文</a>
</p>

# Sonic Link ggwave donor pin

- Upstream: https://github.com/ggerganov/ggwave
- Commit: `060aec73dd7123ccac200442f75bdc7369795ffe`
- Contents: upstream source, public header, required FFT and Reed-Solomon source, and upstream license notices.
- Sonic Link has not patched donor behavior in this phase.
- The root `CMakeLists.txt` is a minimal Sonic Link build wrapper for the curated source subset; the donor C++ source remains byte-for-byte upstream.

The upstream `LICENSE` and `src/reed-solomon/LICENSE` files are preserved.
