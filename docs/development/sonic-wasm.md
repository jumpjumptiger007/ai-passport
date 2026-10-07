<p align="right">
  <a href="sonic-wasm.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Sonic Link WASM validation

This validation builds the production `sonic_core.c` directly for native and
WebAssembly targets, then compares their canonical transcripts byte for byte.
The same script also compiles the pinned stock ggwave donor into WebAssembly
and runs a full binary 40-byte encode/decode loop under Node.js. The waveform
test uses the stock donor implementation and preserves bytes after embedded
NULs.

## Run

Install or activate an Emscripten SDK in a user-selected temporary directory.
No project source or machine-level shell configuration is changed by the
validator. Set `EM_CONFIG` to the SDK's `.emscripten` file and provide the
compiler paths if they are not on `PATH`:

```sh
EM_CONFIG=/path/to/emsdk/.emscripten \
EMCC=/path/to/emsdk/upstream/emscripten/emcc \
EMXX=/path/to/emsdk/upstream/emscripten/em++ \
NODE=/path/to/node \
./tools/validate-wasm.sh
```

`CC` and `CXX` optionally select the native compilers. Build artifacts and
transcripts live in a temporary directory and are removed when the command
finishes. G05 passes only when the native and WASM transcripts match. G06
passes only when the stock ggwave WASM transmitter and receiver recover all 40
bytes exactly, including embedded NUL and high-bit bytes.

Run `./tools/validate.sh --static` for the complete host/static repository
validation. The WASM build is a separate explicit command and is not inferred
from a native-only run. Physical Passport acoustic checks remain hardware
gates and are not covered by this host-side WASM validation.
