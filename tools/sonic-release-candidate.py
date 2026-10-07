#!/usr/bin/env python3
"""Generate auditable Sonic Link candidate metadata; this tool never deploys."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import urlsplit

APP = "FoloToy-AI-Passport"
PIN = "060aec73dd7123ccac200442f75bdc7369795ffe"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git(repo: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--firmware-archive", type=Path, help="verified tools/archive_firmware.py output directory")
    parser.add_argument("--output-dir", type=Path, help="default: build/sonic-release")
    parser.add_argument("--deployed-url", help="only set after deployment identity is verified")
    parser.add_argument("--deployment-verified", action="store_true")
    parser.add_argument("--pages-run-id", help="successful GitHub Actions Pages workflow run")
    parser.add_argument("--pages-deployment-id", help="GitHub Pages deployment identity")
    parser.add_argument("--pages-source-revision", help="source revision used for the verified Pages deployment")
    parser.add_argument("--dns-target", help="Cloudflare DNS-only CNAME target")
    parser.add_argument("--dns-proxied", action="store_true", help="set only if Cloudflare proxy is enabled")
    args = parser.parse_args()
    repo = args.repo.resolve()
    web = repo / "web-companion"
    out = (args.output_dir or repo / "build/sonic-release").resolve()
    try:
        status = git(repo, "status", "--porcelain", "--untracked-files=all")
        revision = git(repo, "rev-parse", "HEAD")
        if args.deployment_verified and not args.deployed_url:
            raise ValueError("--deployment-verified requires --deployed-url")
        if args.deployment_verified and not all((args.pages_run_id, args.pages_deployment_id, args.pages_source_revision, args.dns_target)):
            raise ValueError("verified deployment metadata requires Pages run/deployment/revision and DNS target")
        if args.deployment_verified and args.deployed_url != "https://sonic.yliu.tech":
            raise ValueError("the verified production URL must be https://sonic.yliu.tech")
        if args.deployment_verified and not re.fullmatch(r"[0-9a-f]{40}", args.pages_source_revision):
            raise ValueError("Pages source revision must be a full commit SHA")
        if args.deployment_verified and not (args.pages_run_id.isdecimal() and args.pages_deployment_id.isdecimal()):
            raise ValueError("Pages run and deployment identities must be numeric IDs")
        if args.deployment_verified and (args.dns_target != "jumpjumptiger007.github.io" or args.dns_proxied):
            raise ValueError("Cloudflare DNS must remain DNS-only and target jumpjumptiger007.github.io")
        if args.deployed_url and not args.deployed_url.startswith("https://"):
            raise ValueError("deployed URL must use HTTPS")
        if args.deployment_verified and len(args.deployed_url.encode("utf-8")) > 93:
            raise ValueError("deployed URL exceeds 93 UTF-8 bytes")

        archive = args.firmware_archive.resolve() if args.firmware_archive else None
        if archive is None:
            archives = [p for p in (repo / "build/firmware").iterdir() if p.is_dir() and (p / "manifest.json").is_file()]
            if not archives:
                raise ValueError("no verified firmware archive found; pass --firmware-archive")
            archive = max(archives, key=lambda p: (p / "manifest.json").stat().st_mtime).resolve()
        archive_meta = json.loads((archive / "manifest.json").read_text())
        files = archive_meta["files"]
        full_name = f"{APP}-full.bin"
        elf_name = f"{APP}.elf"
        full_path, elf_path = archive / full_name, archive / elf_name
        if sha256(full_path) != files[full_name]["sha256"] or sha256(elf_path) != files[elf_name]["sha256"]:
            raise ValueError("firmware archive hashes do not match its verified manifest")

        version = json.loads((web / "dist/version.json").read_text())
        sw = (web / "dist/sw.js").read_text()
        cache = re.search(r"const CACHE=(\"[^\"]+\")", sw)
        if cache is None:
            raise ValueError("production service-worker cache identity is missing")
        cache_name = json.loads(cache.group(1))
        web_files = sorted(path for path in (web / "dist").rglob("*") if path.is_file())
        entries = [{"path": path.relative_to(web / "dist").as_posix(), "sha256": sha256(path)} for path in web_files]
        out.mkdir(parents=True, exist_ok=True)
        web_manifest = out / "web-files.sha256"
        web_manifest.write_text("".join(f"{entry['sha256']}  {entry['path']}\n" for entry in entries))
        bundle_digest = hashlib.sha256(web_manifest.read_bytes()).hexdigest()
        kconfig = (repo / "main/Kconfig.projbuild").read_text()
        demo_match = re.search(r'config SONIC_DEMO_URL\s+string[^\n]*\s+default\s+"([^"]*)"', kconfig)
        if demo_match is None:
            raise ValueError("SONIC_DEMO_URL default was not found in Kconfig")
        demo_url = demo_match.group(1)
        demo_bytes = demo_url.encode("utf-8")
        parsed_demo = urlsplit(demo_url)
        if parsed_demo.scheme not in ("http", "https") or not parsed_demo.hostname or len(demo_bytes) > 93:
            raise ValueError("configured Demo URL must be a valid HTTP(S) URL of at most 93 UTF-8 bytes")
        if demo_bytes not in elf_path.read_bytes():
            raise ValueError("verified firmware ELF does not contain the configured Demo URL")
        if args.deployment_verified and demo_url != args.deployed_url:
            raise ValueError("firmware source Demo URL is not the verified deployed URL")

        manifest = {
            "schema_version": 1,
            "release_status": "IMPLEMENTATION COMPLETE / HARDWARE UNVERIFIED" if args.deployment_verified else "CANDIDATE PREDEPLOYMENT / HARDWARE UNVERIFIED",
            "candidate_status": "predeployment" if not args.deployment_verified else "url_paired_candidate",
            "source_revision": revision,
            "source_dirty": bool(status),
            "sonic_version": "1.0.0",
            "ggwave_commit": PIN,
            "ggwave_display_version": "0.4.3",
            "acoustic_profile": "AUDIBLE_FASTEST",
            "acoustic_profile_status": "candidate",
            "firmware": {
                "archive_path": str(archive.relative_to(repo)) if archive.is_relative_to(repo) else str(archive),
                "full_image_path": str(full_path.relative_to(repo)) if full_path.is_relative_to(repo) else str(full_path),
                "full_image_sha256": files[full_name]["sha256"],
                "elf_path": str(elf_path.relative_to(repo)) if elf_path.is_relative_to(repo) else str(elf_path),
                "elf_sha256": files[elf_name]["sha256"],
                "app_elf_fingerprint_4b": archive_meta["app_elf_sha256"][:8],
                "idf_version": archive_meta["app_descriptor"]["idf_version"],
                "demo_url": args.deployed_url if args.deployment_verified else demo_url,
                "candidate_volume": "uncalibrated current candidate",
            },
            "web": {
                "source_revision": revision,
                "package_version": version["packageVersion"],
                "build_id": version["buildId"],
                "version": version["version"],
                "cache_name": cache_name,
                "files_manifest": str(web_manifest.relative_to(repo)) if web_manifest.is_relative_to(repo) else str(web_manifest),
                "file_count": len(entries),
                "bundle_digest_sha256": bundle_digest,
                "deployed_url": args.deployed_url if args.deployment_verified else None,
                "deployment_verified": bool(args.deployment_verified),
            },
            "hosting": {
                "provider": "GitHub Pages" if args.deployment_verified else None,
                "repository": "jumpjumptiger007/ai-passport" if args.deployment_verified else None,
                "branch": "feature/sonic-link" if args.deployment_verified else None,
                "workflow": ".github/workflows/sonic-link-pages.yml" if args.deployment_verified else None,
                "workflow_run_id": args.pages_run_id if args.deployment_verified else None,
                "deployment_id": args.pages_deployment_id if args.deployment_verified else None,
                "source_revision": args.pages_source_revision if args.deployment_verified else None,
                "production_url": args.deployed_url if args.deployment_verified else None,
                "custom_domain": "sonic.yliu.tech" if args.deployment_verified else None,
            },
            "dns": {
                "provider": "Cloudflare" if args.deployment_verified else None,
                "record_type": "CNAME" if args.deployment_verified else None,
                "hostname": "sonic.yliu.tech" if args.deployment_verified else None,
                "target": args.dns_target if args.deployment_verified else None,
                "proxied": bool(args.dns_proxied) if args.deployment_verified else None,
            },
            "compatibility": {
                "protocol_version": 1,
                "frame_size": 40,
                "max_message_bytes": 93,
                "payload_types": {"TEXT": 1, "URL": 2, "TOKEN": 3, "DEVICE_INFO": 4},
                "demo_url_utf8_bytes": len(demo_bytes),
                "demo_url_frame_count": max(1, (len(demo_bytes) + 30) // 31),
                "candidate_profile_match": True,
            },
            "gate_status": {
                "G11": "PARTIAL / candidate compatibility established; shipping profile awaits G22",
                "G12": "PASS" if args.deployment_verified else "NOT RUN / deployment target and access absent",
                "G13": "NOT RUN",
                "G14": "NOT RUN",
                "G15": "NOT RUN",
                "G16": "NOT RUN",
                "G17": "NOT RUN",
                "G18": "NOT RUN",
                "G19": "NOT RUN",
                "G20": "PARTIAL / Web media-track and Worker lifecycle tests PASS; Passport transition and heap-drift measurements NOT RUN",
                "G21": "NOT RUN",
                "G22": "NOT RUN",
                "G24": "PASS / official verified archive, full-image hash, and matching ELF fingerprint recorded" if args.deployment_verified else "NOT RUN / final URL-paired archive identity not generated",
                "G25": "NOT RUN",
            },
        }
        target = out / "sonic-link-candidate.json"
        target.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        print(f"Candidate metadata: {target}")
        print(f"Web build: {version['version']} ({len(entries)} files, SHA-256 {bundle_digest})")
        print(f"Firmware archive: {archive}")
        print(f"Deployment verified: {bool(args.deployment_verified)}")
    except (OSError, KeyError, TypeError, ValueError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
