# gpx2fit: v2 Task Breakdown (Activity Types, Multi-Arch Releases & Web/WASM)

## Tools
You can find the FITCSVTool.jar in tests/tools/

Scope: **Add Garmin activity/sport selection, automate cross-platform binary releases via GitHub Actions, and compile the core C library to WebAssembly for a client-side web app.**

Each task fits in one small working session. Do the tasks in order unless noted. Keep `course.h` and the task's own files in context, and little else.

---

## Phase 15: Client-Side Web Application (`web/`)

Zero-backend static app (hostable on GitHub Pages / Cloudflare Pages) where GPX files never leave the user's browser.

- [x] **15.1 Minimalist UI & file dropzone (`web/index.html`, `web/app.js`, `web/style.css`)**
  - Needs: `web/dist/gpx2fit.js`
  - Output: drag-and-drop `.gpx` picker + "Load sample CalTopo route" button (`caltopo_sample.gpx`)[cite: 2, 5], primary controls (Course Name, Activity Type dropdown for all 7 sports, Pace input supporting `min/km` and `km/h`, Snap distance slider/input), and one-click `.fit` download button.
  - Done when: dropping a GPX file converts and

- [x] **15.2 Interactive route map & waypoint table**
  - Needs: `web/app.js`
  - Output: Leaflet (or MapLibre) map displaying the GPX polyline, snapped course point markers (color/icon-coded by FIT type), and skipped waypoints (e.g., `Caution` at 477 m[cite: 2, 5]) with a live table showing distance (km), offset (m), and assigned icon type.
  - Done when: adjusting `--snap` or `--pass` updates the map markers and skipped-waypoint warnings in real time[cite: 5].

- [x] **15.3 Advanced options & Garmin transfer instructions**
  - Needs: `web/index.html`, `web/app.js`
  - Output: collapsible "Advanced" drawer for `--pass` (`nearest`, `ordered`, `first`, `last`), `--max-points`, `--start` UTC date/time, `--strict`, and custom `--map` rule editor (`keyword = type`)[cite: 5]; plus post-download instructions for Garmin Connect (*Training & Planning → Courses → Import*) and USB (`GARMIN/NewFiles`)[cite: 5].
  - Done when: custom map rules and advanced flags work end-to-end.

- [x] **15.4 Service Worker + `version.json` update detector & reload modal (`web/sw.js`, `web/version.json`)**
  - Needs: `web/index.html`, `web/app.js`, `Makefile`
  - Output:
    - Build step stamps `web/version.json` (git SHA / build timestamp) and injects the cache version into `web/sw.js`.
    - Service Worker caches app shell + `gpx2fit.wasm` for fast/offline loads, uses `skipWaiting` on message (`SKIP_WAITING`), and checks `./version.json` (`cache: "no-store"`) on page load, visibility focus, and a periodic interval.
    - When a new Service Worker is waiting or `./version.json` differs from the running build ID, display a non-intrusive modal/dialog banner: *"A new version of gpx2fit is available."* with a **"Reload"** button that activates the new Service Worker and reloads the page cleanly.
  - Done when: bumping `version.json` / rebuilding triggers the update dialog immediately on an open tab and clicking **Reload** loads the updated version.

---

## Phase 16: GitHub Pages, Multi-Arch `httpd:alpine` Docker Image & HAProxy Docs

- [x] **16.1 GitHub Pages deployment workflow (`.github/workflows/pages.yml`)**
  - Needs: `Makefile`, `web/`
  - Output: GitHub Actions workflow triggered on pushes to `main` (touching `src/`, `data/`, `web/`, or `Makefile`) that compiles `make wasm` + `web/` bundle and deploys to GitHub Pages (`https://vloschiavo.github.io/gpx2fit/`).
  - Done when: `https://vloschiavo.github.io/gpx2fit/` loads cleanly, instantiates `gpx2fit.wasm`, and converts a GPX file.

- [ ] **16.2 Minimal `httpd:alpine` Dockerfile & Apache config (`docker/Dockerfile`, `docker/httpd-gpx2fit.conf`)**
  - Needs: `Makefile`, `web/`
  - Output:
    - Multi-stage `docker/Dockerfile`: builder stage compiles WASM/web assets; final runtime stage uses `httpd:alpine` copying only the static `web/` output and Apache config snippet.
    - `docker/httpd-gpx2fit.conf`:
      - `AddType application/wasm .wasm`
      - `X-Content-Type-Options: nosniff` header
      - `Cache-Control: no-cache, must-revalidate` for `index.html`, `sw.js`, and `version.json` (so update checks always hit origin immediately), and `Cache-Control: public, max-age=3600` for `.wasm`/`.js`/`.css` assets
      - `mod_remoteip` configured (`RemoteIPHeader X-Forwarded-For`, trusted private proxy ranges) and log format recording client IP + `X-Forwarded-Proto`.
  - Done when: `docker build -f docker/Dockerfile -t gpx2fit-web:local .` produces a minimal image (< 20 MB) that serves `application/wasm` and passes a `curl -I` header check.

- [ ] **16.3 Multi-arch Docker Hub CI workflow (`.github/workflows/docker.yml`)**
  - Needs: `docker/Dockerfile`
  - Output: GitHub Actions workflow using `docker/setup-qemu-action` and `docker/setup-buildx-action` to build and push `linux/amd64` and `linux/arm64` images to `docker.io/vloschiavo/gpx2fit` on `v*` tags and `main` (using `DOCKERHUB_USERNAME` and `DOCKERHUB_TOKEN` repository secrets).
  - Done when: multi-arch manifest on Docker Hub lists both `linux/amd64` and `linux/arm64` and pulls/runs cleanly.

- [ ] **16.4 Document self-hosting & HAProxy SSL offload in `README.md`**
  - Needs: `README.md`, `docker/Dockerfile`, `docker/httpd-gpx2fit.conf`
  - Output: `README.md` section covering:
    - Running the `vloschiavo/gpx2fit` `httpd:alpine` container (`docker run` / `docker-compose.yml` snippet)
    - Complete HAProxy frontend/backend snippet for SSL termination forwarding `X-Forwarded-For` (`option forwardfor`) and `X-Forwarded-Proto https` (`http-request set-header X-Forwarded-Proto https if { ssl_fc }`).
  - Done when: `README.md` clearly documents CLI, GitHub Pages, Docker self-hosting, and HAProxy configuration.
  
## Later (optional)

- [ ] Support the same waypoint in both directions on out and back courses
- [ ] Turn-by-turn cue generation from track geometry.
