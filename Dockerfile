# ==============================================================================
# Stage 1: Build stage (compile C++ binary)
# ==============================================================================
FROM debian:sid-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools and development headers
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libwpewebkit-2.0-dev \
    libwpebackend-fdo-1.0-dev \
    libwpe-1.0-dev \
    libsoup-3.0-dev \
    libglib2.0-dev \
    libxkbcommon-dev \
    libegl-dev \
    libepoxy-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy CMakeLists.txt and source files
COPY CMakeLists.txt /app/
COPY src/ /app/src/

# Compile Release build with maximum optimizations
RUN cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /app/build -j$(nproc)

# Strip binary to keep final image size minimal
RUN strip --strip-all /app/build/wpe-url-extractor

# ==============================================================================
# Stage 2: Minimal Runtime stage for Render
# ==============================================================================
FROM debian:sid-slim AS runner

ENV DEBIAN_FRONTEND=noninteractive

# Install only minimal runtime libraries (no compilers or dev headers)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libwpewebkit-2.0-1 \
    libwpebackend-fdo-1.0-1 \
    libwpe-1.0-1 \
    libsoup-3.0-0 \
    libglib2.0-0t64 \
    libgles2 \
    libegl1 \
    libgl1-mesa-dri \
    gstreamer1.0-plugins-bad \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/* \
    && for d in /usr/lib/*-linux-gnu; do \
         if [ -f "$d/libWPEBackend-fdo-1.0.so.1" ] && [ ! -e "$d/libWPEBackend-fdo-1.0.so" ]; then \
           ln -sf "$d/libWPEBackend-fdo-1.0.so.1" "$d/libWPEBackend-fdo-1.0.so"; \
         fi; \
       done

# Configure Environment for Render & Containerization:
# 1. WebKit sandbox MUST be disabled in unprivileged container environments like Render
ENV WEBKIT_FORCE_SANDBOX=0
ENV WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1
# 2. Software headless rendering (no physical GPU in standard cloud containers)
ENV LIBGL_ALWAYS_SOFTWARE=1
ENV WEBKIT_DISABLE_DMABUF_RENDERER=1
ENV WPE_BACKEND_LIBRARY=libWPEBackend-fdo-1.0.so.1
# 3. Default Render port (Render injects $PORT at runtime)
ENV PORT=8080

# Create unprivileged application user
RUN useradd -m -u 1000 -s /bin/bash appuser

WORKDIR /home/appuser

# Copy executable from builder stage
COPY --from=builder /app/build/wpe-url-extractor /usr/local/bin/wpe-url-extractor

# Copy entrypoint script
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

USER appuser

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD curl -f http://127.0.0.1:${PORT:-8080}/health || exit 1

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
