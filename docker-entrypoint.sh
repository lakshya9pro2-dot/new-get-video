#!/bin/sh
set -e

# Render provides the port in the $PORT environment variable. Default to 8080 if unset.
PORT="${PORT:-8080}"

# If no arguments provided, launch in HTTP API server mode using $PORT
if [ "$#" -eq 0 ]; then
    echo "[INFO] Starting WPE HLS Extractor HTTP server on port ${PORT}..."
    exec /usr/local/bin/wpe-url-extractor --server --port "$PORT"
fi

# If arguments start with a flag (e.g. --verbose), pass them to server mode
if [ "${1#-}" != "$1" ]; then
    echo "[INFO] Starting WPE HLS Extractor HTTP server on port ${PORT} with options $@..."
    exec /usr/local/bin/wpe-url-extractor --server --port "$PORT" "$@"
fi

# Otherwise, execute user command or single-shot extraction CLI
exec /usr/local/bin/wpe-url-extractor "$@"
