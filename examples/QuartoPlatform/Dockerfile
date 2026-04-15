FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y \
    build-essential cmake git pkg-config \
    liburing-dev libssl-dev libpq-dev \
    libcurl4-openssl-dev libhiredis-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release \
             -DSERVERCORE_BUILD_TESTS=OFF && \
    cmake --build . -j$(nproc) --target quarto_platform

FROM ubuntu:24.04 AS gateway

RUN apt-get update && apt-get install -y --no-install-recommends \
    liburing2 libssl3t64 libcurl4t64 libhiredis1.1.0t64 curl && \
    rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/bin/quarto_platform /usr/local/bin/quarto-server
COPY examples/QuartoPlatform/config/gateway.yaml /etc/quarto/gateway.yaml

EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=5s --start-period=5s \
    CMD curl -f http://localhost:8080/health || exit 1
CMD ["quarto-server", "--config", "/etc/quarto/gateway.yaml"]

FROM ubuntu:24.04 AS editor

ARG QUARTO_VERSION=1.6.42
RUN apt-get update && apt-get install -y --no-install-recommends \
    liburing2 libssl3t64 libcurl4t64 curl wget && \
    wget -q "https://github.com/quarto-dev/quarto-cli/releases/download/v${QUARTO_VERSION}/quarto-${QUARTO_VERSION}-linux-amd64.deb" && \
    dpkg -i "quarto-${QUARTO_VERSION}-linux-amd64.deb" && \
    rm "quarto-${QUARTO_VERSION}-linux-amd64.deb" && \
    apt-get purge -y wget && \
    rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/bin/quarto_platform /usr/local/bin/quarto-server
COPY examples/QuartoPlatform/config/editor.yaml /etc/quarto/editor.yaml

EXPOSE 8080 4000
HEALTHCHECK --interval=10s --timeout=5s --start-period=30s \
    CMD curl -f http://localhost:8080/health || exit 1
CMD ["quarto-server", "--config", "/etc/quarto/editor.yaml"]
