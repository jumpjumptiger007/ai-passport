#!/usr/bin/env python3
"""Check Sonic Link source/build compatibility for a release candidate."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from urllib.parse import urlsplit


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def check(repo: Path, manifest_path: Path) -> None:
    core = (repo / "components/sonic_core/include/sonic_core.h").read_text()
    app = (repo / "main/sonic_app.cpp").read_text()
    profile = (repo / "components/sonic_ggwave_profile/sonic_ggwave_profile.cpp").read_text()
    profile_header = (repo / "components/sonic_ggwave_profile/include/sonic_ggwave_profile.h").read_text()
    web_profile = (repo / "web-companion/src/wasm/ggwave-web-wrapper.cpp").read_text()
    web_main = (repo / "web-companion/src/main.ts").read_text()
    pin_doc = (repo / "ggwave_vendor/SONIC-LINK-PIN.md").read_text()
    web_version = json.loads((repo / "web-companion/dist/version.json").read_text())
    sw = (repo / "web-companion/dist/sw.js").read_text()
    manifest = json.loads(manifest_path.read_text())
    dist = repo / "web-companion/dist"
    kconfig = (repo / "main/Kconfig.projbuild").read_text()
    demo_match = re.search(r'config SONIC_DEMO_URL\s+string[^\n]*\s+default\s+"([^"]*)"', kconfig)
    require(demo_match is not None, "SONIC_DEMO_URL Kconfig default is missing")
    demo_url = demo_match.group(1)
    demo_bytes = demo_url.encode("utf-8")
    parsed_demo = urlsplit(demo_url)
    require(parsed_demo.scheme in ("http", "https") and parsed_demo.hostname, "configured Demo URL must be a valid HTTP(S) URL")
    require(0 < len(demo_bytes) <= 93, "configured Demo URL must fit the 93-byte logical payload")

    for macro, expected in (("SONIC_PROTOCOL_VERSION", 1), ("SONIC_FRAME_SIZE", 40), ("SONIC_MAX_MESSAGE_BYTES", 93)):
        match = re.search(rf"#define\s+{macro}\s+(\d+)u?\b", core)
        require(match is not None and int(match.group(1)) == expected, f"{macro} must equal {expected}")

    types = {name: int(value) for name, value in re.findall(r"SONIC_TYPE_(TEXT|URL|TOKEN|DEVICE_INFO)\s*=\s*(\d+)", core)}
    require(types == {"TEXT": 1, "URL": 2, "TOKEN": 3, "DEVICE_INFO": 4}, "firmware payload type mapping changed")
    require(all(name in web_main for name in ("TEXT", "URL", "TOKEN", "DEVICE_INFO")), "Web payload semantics are incomplete")
    require("case SONIC_TYPE_TEXT:" in (repo / "components/sonic_core/sonic_core.c").read_text(), "firmware TEXT validation is missing")
    require("case SONIC_TYPE_URL:" in (repo / "components/sonic_core/sonic_core.c").read_text(), "firmware URL validation is missing")
    require("case SONIC_TYPE_TOKEN:" in (repo / "components/sonic_core/sonic_core.c").read_text(), "firmware TOKEN validation is missing")

    pin = "060aec73dd7123ccac200442f75bdc7369795ffe"
    require(pin in pin_doc, "ggwave source pin does not match the approved commit")
    require('kGGWaveVersion[] = "0.4.3"' in app, "firmware ggwave display version changed")
    require("kAcousticProfile = 1u" in app and "AUDIBLE_FASTEST candidate" in app, "firmware profile is not AUDIBLE_FASTEST candidate")
    require("GGWAVE_PROTOCOL_AUDIBLE_FASTEST" in profile and "AudibleFastest = 0" in profile_header, "firmware profile mapping changed")
    require("Candidate::AudibleFastest" in web_profile and "protocol_id(kCandidate" in web_profile, "Web profile is not AUDIBLE_FASTEST")
    require("AUDIBLE_FASTEST candidate" in web_main, "Web diagnostics do not identify the candidate profile")
    require("Build ID" in web_main, "Web diagnostics do not expose build identity")
    prepare = profile.split("bool prepare", 1)[1].split("bool required_heap_bytes", 1)[0]
    require("configure_protocols(candidate)" in prepare and "return false" in prepare, "firmware must fail instead of automatically falling back")
    require("constexpr sonic_ggwave_profile::Candidate kCandidate =\n    sonic_ggwave_profile::Candidate::AudibleFastest;" in web_profile, "Web must pin one candidate without fallback")

    require(manifest["compatibility"]["protocol_version"] == 1, "manifest protocol version mismatch")
    require(manifest["compatibility"]["frame_size"] == 40, "manifest frame size mismatch")
    require(manifest["compatibility"]["max_message_bytes"] == 93, "manifest payload size mismatch")
    require(manifest["compatibility"]["payload_types"] == types, "manifest payload types mismatch")
    require(manifest["compatibility"]["candidate_profile_match"] is True, "candidate profile mismatch")
    require(manifest["firmware"]["demo_url"] == demo_url, "manifest Demo URL differs from tracked firmware configuration")
    require(manifest["compatibility"]["demo_url_utf8_bytes"] == len(demo_bytes), "manifest Demo URL byte length is stale")
    require(manifest["compatibility"]["demo_url_frame_count"] == max(1, (len(demo_bytes) + 30) // 31), "manifest Demo URL frame count is stale")
    require(manifest["web"]["build_id"] == web_version["buildId"], "manifest Web build identity is stale")
    require(manifest["web"]["version"] == web_version["version"], "manifest Web version is stale")
    cache_match = re.search(r"const CACHE=(\"[^\"]+\")", sw)
    require(cache_match is not None and json.loads(cache_match.group(1)) == f"sonic-link-{web_version['version']}", "service-worker cache identity mismatch")
    require(manifest["web"]["cache_name"] == json.loads(cache_match.group(1)), "manifest service-worker cache identity is stale")
    if manifest["web"]["deployment_verified"]:
        hosting = manifest["hosting"]
        dns = manifest["dns"]
        require(hosting["provider"] == "GitHub Pages", "verified release must record GitHub Pages hosting")
        require(hosting["repository"] == "jumpjumptiger007/ai-passport", "Pages repository identity mismatch")
        require(hosting["branch"] == "feature/sonic-link", "Pages branch identity mismatch")
        require(hosting["workflow"] == ".github/workflows/sonic-link-pages.yml", "Pages workflow identity mismatch")
        require(hosting["production_url"] == demo_url == manifest["web"]["deployed_url"], "deployed URL differs across release artifacts")
        require(hosting["custom_domain"] == parsed_demo.hostname, "Pages custom domain differs from the Demo URL")
        require(all(hosting[key] for key in ("workflow_run_id", "deployment_id", "source_revision")), "Pages deployment identity is incomplete")
        require(dns["provider"] == "Cloudflare" and dns["record_type"] == "CNAME", "DNS provider or record type mismatch")
        require(dns["hostname"] == parsed_demo.hostname and dns["target"] == "jumpjumptiger007.github.io", "Cloudflare Pages CNAME target mismatch")
        require(dns["proxied"] is False, "Cloudflare CNAME must remain DNS only for this release")
        require(manifest["gate_status"]["G12"] == "PASS", "verified deployment must record G12 PASS")
        require(manifest["gate_status"]["G24"].startswith("PASS"), "URL-paired verified archive must record G24 PASS")
    files_manifest = repo / manifest["web"]["files_manifest"]
    raw_files_manifest = files_manifest.read_bytes()
    require(hashlib.sha256(raw_files_manifest).hexdigest() == manifest["web"]["bundle_digest_sha256"], "Web bundle digest is stale")
    listed = {}
    for line in raw_files_manifest.decode("utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([^\r\n]+)", line)
        require(match is not None, "Web file manifest contains an invalid line")
        listed[match.group(2)] = match.group(1)
    actual_paths = {path.relative_to(dist).as_posix() for path in dist.rglob("*") if path.is_file()}
    require(set(listed) == actual_paths, "Web bundle file manifest does not match production files")
    for name, expected_hash in listed.items():
        require(hashlib.sha256((dist / name).read_bytes()).hexdigest() == expected_hash, f"Web asset digest mismatch: {name}")
    required_assets = (
        "wasm/ggwave.js", "wasm/ggwave.wasm", "wasm/sonic-core.js", "wasm/sonic-core.wasm",
    )
    require(all((dist / name).is_file() for name in required_assets), "production bundle is missing local WASM assets")
    require(any("decoder-worker-" in name for name in listed), "production bundle is missing the decoder Worker")
    require(any("capture-processor-" in name for name in listed), "production bundle is missing the audio Worklet")
    external_patterns = ("fonts.googleapis.com", "fonts.gstatic.com", "googletagmanager.com", "google-analytics.com", "segment.io", "mixpanel.com")
    for name in ("index.html", "manifest.webmanifest", *[path for path in listed if path.endswith(".css")]):
        content = (dist / name).read_text(errors="replace")
        require(not re.search(r"(?:src|href)=['\"]https?://|url\(['\"]?https?://|@import\s+url\(['\"]?https?://", content, re.I), f"external runtime asset reference in {name}")
        require(not any(domain in content.lower() for domain in external_patterns), f"remote service dependency found in {name}")
    require(manifest["acoustic_profile"] == "AUDIBLE_FASTEST" and manifest["acoustic_profile_status"] == "candidate", "profile must remain a candidate")
    print("Sonic Link release-pair compatibility: PASS (candidate; deployment/profile selection not implied)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        check(args.repo.resolve(), args.manifest.resolve())
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
