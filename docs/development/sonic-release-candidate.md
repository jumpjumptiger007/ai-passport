<p align="right">
  <strong>English</strong> · <a href="sonic-release-candidate.zh_CN.md">简体中文</a>
</p>

# Sonic Link release candidate

Phase 10 creates a reproducible candidate pair. `AUDIBLE_FASTEST` remains a
candidate until G22 selects the shipping profile, and the speaker volume stays
uncalibrated until G21.

## Generate local candidate evidence

Build and validate `web-companion/` using its documented npm scripts, then run
the repository's official firmware validation under ESP-IDF 5.5.3. The firmware
archive is retained under `build/firmware/` by that validation command.

From the repository root:

```sh
python3 tools/sonic-release-candidate.py
python3 tools/check-sonic-release-pair.py --manifest build/sonic-release/sonic-link-candidate.json
```

The generator records source revision and dirty state, verified firmware
archive hashes, Web version/cache identity, a SHA-256 manifest of every
production Web file, and the current Demo URL. It never deploys. Before a real
deployment, the metadata explicitly describes the firmware as predeployment;
its placeholder URL is not a matched release pair.

## Deployment and URL pairing

Deploy only the validated `web-companion/dist/` to an explicitly selected,
authorized static HTTPS site. Verify the site's `version.json`, worker,
worklet, WASM files, manifest, service worker, cache identity, and browser smoke
against the local build. Then set the single tracked `SONIC_DEMO_URL` Kconfig
default to that exact verified HTTPS URL, rebuild with
`./tools/validate.sh --firmware` under ESP-IDF 5.5.3, and regenerate metadata
with the verified deployment URL. The generator rejects a URL that differs
from the firmware configuration.

No hosting provider, project, domain, DNS ownership, or credentials are implied
by this repository. Do not deploy until the target and access are established.

## Status language

Candidate metadata is not a final release report. Hardware and physical
acoustic gates that have not been run remain `NOT RUN`. A software-complete
candidate may be reported only as `IMPLEMENTATION COMPLETE / HARDWARE
UNVERIFIED`; do not report `DEMO COMPLETE` or claim that a shipping acoustic
profile or volume has been selected.
