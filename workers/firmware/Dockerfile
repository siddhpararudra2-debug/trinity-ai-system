# Trinity Firmware Compilation Sandbox
# Isolated build environment for deterministic firmware generation.
# Security: no network, non-root, read-only root, resource limits.
FROM python:3.11-slim AS runtime

RUN groupadd -r firmware && useradd -r -g firmware -d /work firmware

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    g++ \
    make \
    cmake \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --no-cache-dir platformio esptool

WORKDIR /work
RUN chown firmware:firmware /work

USER firmware

COPY --chown=firmware:firmware entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
