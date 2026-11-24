# gpx2fit: v2 Task Breakdown (Activity Types, Multi-Arch Releases & Web/WASM)

## Tools
You can find the FITCSVTool.jar in tests/tools/

Scope: **Add Garmin activity/sport selection, automate cross-platform binary releases via GitHub Actions, and compile the core C library to WebAssembly for a client-side web app.**

Each task fits in one small working session. Do the tasks in order unless noted. Keep `course.h` and the task's own files in context, and little else.

---

- [ ] **16.5 Fix Docker Hub tag set: `latest` + per-arch `amd64`/`arm64` tags**
  - Needs: `.github/workflows/docker.yml` (16.3), `README.md` (16.4)
  - Scope: `.github/workflows/docker.yml` and `README.md` only. Do not modify `docker/Dockerfile` or check off this item in `PLAN.md` until a live workflow run passes.
  - Changes:
    - In `.github/workflows/docker.yml`, add `type=raw,value=latest,enable={{is_default_branch}}` (and `type=raw,value=main,enable={{is_default_branch}}`) to `docker/metadata-action` tags while keeping existing `main`, `sha-<short>`, `v*`, and semver tags.
    - Add two single-platform `docker/build-push-action` steps reusing the build context, `docker/Dockerfile`, `WEB_*` build args, and labels: one for `linux/amd64` -> `docker.io/vloschiavo/gpx2fit:amd64`, one for `linux/arm64` -> `docker.io/vloschiavo/gpx2fit:arm64`.
    - Do not use `steps.meta.outputs.tags` in the single-arch steps; isolate their GHA cache keys/scopes so they do not conflict with the multi-arch build.
    - Add a CI verification step running `docker buildx imagetools inspect` on `:latest`, `:amd64`, and `:arm64` to assert expected architectures are present.
    - Update `README.md` (~lines 361-386) with the full tag matrix and an `imagetools inspect` example.
  - Done when:
    - `docker buildx imagetools inspect docker.io/vloschiavo/gpx2fit:latest` shows `linux/amd64` and `linux/arm64`.
    - `:amd64` and `:arm64` inspect as single-arch manifests (`linux/amd64` and `linux/arm64` respectively).
    - Existing `main`, `sha-*`, and `v*` tag behavior remains intact.

## Later (optional)

- [ ] Support the same waypoint in both directions on out and back courses
- [ ] Turn-by-turn cue generation from track geometry.
