#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

IMAGE_NAME="${IMAGE_NAME:-gpx2fit-web:local}"
CONTAINER_NAME="${CONTAINER_NAME:-gpx2fit-web-test}"
HOST_PORT="${PORT:-8080}"

cleanup() {
  if docker ps -q -f "name=^/${CONTAINER_NAME}$" | grep -q .; then
    echo "Stopping ${CONTAINER_NAME}..."
    docker stop "$CONTAINER_NAME" >/dev/null
  fi
}
trap cleanup EXIT INT TERM

# Remove any leftover container with the same name before starting
docker rm -f "$CONTAINER_NAME" >/dev/null 2>&1 || true

echo "Building ${IMAGE_NAME} from ${REPO_ROOT}..."
docker build -f "$SCRIPT_DIR/Dockerfile" -t "$IMAGE_NAME" "$REPO_ROOT"
docker image ls "$IMAGE_NAME"

echo "Starting ${CONTAINER_NAME} on port ${HOST_PORT}..."
docker run --rm -d \
  -p "${HOST_PORT}:80" \
  --name "$CONTAINER_NAME" \
  "$IMAGE_NAME"

# Smoke-check HTTP endpoint (up to ~10 seconds)
URL="http://localhost:${HOST_PORT}/"
READY=0
for _ in $(seq 1 20); do
  if curl -fsS "$URL" >/dev/null 2>&1; then
    READY=1
    break
  fi
  sleep 0.5
done

if [ "$READY" -ne 1 ]; then
  echo "ERROR: Container failed HTTP health check at ${URL}" >&2
  docker logs "$CONTAINER_NAME" >&2 || true
  exit 1
fi

echo "Verified HTTP 200 at ${URL}"
read -r -p "Press Enter to stop the ${CONTAINER_NAME} container..."
