# === C++ build ===
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    liburing-dev libssl-dev \
    libcurl4-openssl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DSERVERCORE_BUILD_TESTS=OFF \
             -DBUILD_EXAMPLES=OFF \
             -DBUILD_STORAGE=OFF && \
    cmake --build . -j$(nproc) --target quarto_platform

# === Frontend build ===
FROM node:22-slim AS frontend

WORKDIR /app
COPY examples/QuartoPlatform/frontend/ ./
RUN npm install

# Production build: empty API base (same-origin), no dev user
RUN VITE_API_BASE= VITE_PREVIEW_BASE= VITE_DEV_USER= npm run build

# === Gateway image ===
FROM ubuntu:24.04 AS gateway

RUN apt-get update && apt-get install -y --no-install-recommends \
    liburing2 libssl3t64 libcurl4t64 curl && \
    rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/bin/quarto_platform /usr/local/bin/quarto-server
COPY --from=frontend /app/dist /srv/frontend
COPY examples/QuartoPlatform/config/gateway.yaml /etc/quarto/gateway.yaml

EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=5s --start-period=5s \
    CMD curl -f http://localhost:8080/health || exit 1
CMD ["quarto-server", "--config", "/etc/quarto/gateway.yaml"]

# === Editor image ===
FROM ubuntu:24.04 AS editor

ARG QUARTO_VERSION=1.9.37
RUN apt-get update && apt-get install -y --no-install-recommends \
    liburing2 libssl3t64 libcurl4t64 curl ca-certificates && \
    curl -fsSL -o /tmp/quarto.deb \
      "https://github.com/quarto-dev/quarto-cli/releases/download/v${QUARTO_VERSION}/quarto-${QUARTO_VERSION}-linux-amd64.deb" && \
    dpkg -i /tmp/quarto.deb && \
    rm /tmp/quarto.deb && \
    rm -rf /var/lib/apt/lists/*

RUN mkdir -p /workspace /published

COPY --from=build /src/build/bin/quarto_platform /usr/local/bin/quarto-server
COPY --from=frontend /app/dist /srv/frontend
COPY examples/QuartoPlatform/config/editor.yaml /etc/quarto/editor.yaml

EXPOSE 8080 4000
HEALTHCHECK --interval=10s --timeout=5s --start-period=30s \
    CMD curl -f http://localhost:8080/health || exit 1
CMD ["quarto-server", "--config", "/etc/quarto/editor.yaml"]
