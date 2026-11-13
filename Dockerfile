FROM debian:bookworm-slim AS builder

ARG ZIG_VERSION=0.17.1
ARG TARGETARCH

RUN apt-get update && apt-get install -y \
    ca-certificates curl xz-utils git \
    && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    case "${TARGETARCH:-amd64}" in \
        amd64) ARCH=x86_64 ;; \
        arm64) ARCH=aarch64 ;; \
        *) echo "Unsupported architecture"; exit 1 ;; \
    esac; \
    curl -fsSL \
        "https://ziglang.org/download/${ZIG_VERSION}/zig-${ARCH}-linux-${ZIG_VERSION}.tar.xz" \
        -o /tmp/zig.tar.xz; \
    tar -xJf /tmp/zig.tar.xz -C /opt; \
    ln -s "/opt/zig-${ARCH}-linux-${ZIG_VERSION}/zig" /usr/local/bin/zig

WORKDIR /app

COPY . .

RUN zig build -Doptimize=ReleaseFast

FROM debian:bookworm-slim

WORKDIR /app

COPY --from=builder /app/zig-out/bin/ZigServer /usr/local/bin/zigserver
COPY --from=builder --chown=10001:10001 /app/public ./public

EXPOSE 8080

USER 10001:10001

ENTRYPOINT ["/usr/local/bin/zigserver"]