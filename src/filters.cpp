#include "filters.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace WpeExtractor {

static std::string toLower(const std::string& str) {
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return s;
}

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

bool isValidUrl(const std::string& url) {
    if (url.empty()) return false;
    std::string lower = toLower(url);

    // Only allow http:// and https:// schemes
    bool is_http = (lower.rfind("http://", 0) == 0);
    bool is_https = (lower.rfind("https://", 0) == 0);

    if (!is_http && !is_https) {
        return false;
    }

    // Explicitly reject forbidden schemes if embedded
    if (lower.find("javascript:") != std::string::npos ||
        lower.find("data:") != std::string::npos ||
        lower.find("file:") != std::string::npos ||
        lower.find("unix:") != std::string::npos ||
        lower.find("about:") != std::string::npos) {
        // Only reject if it appears as scheme
        if (lower.rfind("file://", 0) == 0 ||
            lower.rfind("data:", 0) == 0 ||
            lower.rfind("javascript:", 0) == 0) {
            return false;
        }
    }

    size_t prefix_len = is_http ? 7 : 8;
    if (url.length() <= prefix_len) {
        return false;
    }

    // Host portion must be non-empty
    std::string rest = url.substr(prefix_len);
    if (rest.empty() || rest[0] == '/' || rest[0] == ':') {
        return false;
    }

    return true;
}

std::string resolveUrl(const std::string& base_url, const std::string& relative_url) {
    if (relative_url.empty()) return base_url;
    std::string lower_rel = toLower(relative_url);

    // Already absolute
    if (lower_rel.rfind("http://", 0) == 0 || lower_rel.rfind("https://", 0) == 0) {
        return relative_url;
    }

    // Protocol-relative //example.com/stream.m3u8
    if (lower_rel.rfind("//", 0) == 0) {
        std::string scheme = "http";
        size_t colon = base_url.find(':');
        if (colon != std::string::npos) {
            scheme = base_url.substr(0, colon);
        }
        return scheme + ":" + relative_url;
    }

    // Extract origin: scheme://host[:port]
    size_t proto_end = base_url.find("://");
    if (proto_end == std::string::npos) return relative_url;

    size_t host_end = base_url.find('/', proto_end + 3);
    std::string origin = (host_end == std::string::npos) ? base_url : base_url.substr(0, host_end);

    // Root-relative: /media/stream.m3u8
    if (relative_url[0] == '/') {
        return origin + relative_url;
    }

    // Path-relative
    if (host_end == std::string::npos) {
        return origin + "/" + relative_url;
    }

    size_t last_slash = base_url.rfind('/');
    if (last_slash != std::string::npos && last_slash > proto_end + 2) {
        return base_url.substr(0, last_slash + 1) + relative_url;
    }

    return origin + "/" + relative_url;
}

std::string getOrigin(const std::string& url) {
    if (url.empty()) return "";
    size_t proto_end = url.find("://");
    if (proto_end == std::string::npos) return url;

    size_t host_end = url.find('/', proto_end + 3);
    if (host_end == std::string::npos) {
        return url;
    }
    return url.substr(0, host_end);
}

std::string normalizeMimeType(const std::string& raw_mime) {
    std::string mime = trim(toLower(raw_mime));
    size_t semi = mime.find(';');
    if (semi != std::string::npos) {
        mime = trim(mime.substr(0, semi));
    }
    return mime;
}

bool isHlsMimeType(const std::string& raw_mime) {
    if (raw_mime.empty()) return false;
    std::string norm = normalizeMimeType(raw_mime);

    if (norm == "application/vnd.apple.mpegurl" ||
        norm == "application/x-mpegurl" ||
        norm == "audio/mpegurl" ||
        norm == "audio/x-mpegurl" ||
        norm == "application/mpegurl" ||
        norm == "video/x-mpegurl") {
        return true;
    }

    // Check if contains vnd.apple.mpegurl or x-mpegurl
    if (norm.find("mpegurl") != std::string::npos) {
        return true;
    }

    return false;
}

bool isHlsUrlFallback(const std::string& raw_url) {
    if (raw_url.empty()) return false;
    std::string lower = toLower(raw_url);

    // Exclude definite non-playlist media formats
    if (lower.find(".mp4") != std::string::npos && lower.find(".m3u8") == std::string::npos) return false;
    if (lower.find(".ts") != std::string::npos && lower.find(".m3u8") == std::string::npos) return false;
    if (lower.find(".m4s") != std::string::npos && lower.find(".m3u8") == std::string::npos) return false;
    if (lower.find(".png") != std::string::npos || lower.find(".jpg") != std::string::npos) return false;

    // Check for standard .m3u8 extension
    size_t m3u8_pos = lower.find(".m3u8");
    if (m3u8_pos != std::string::npos) {
        // Verify it occurs at end of path or before query parameters/hash
        size_t after = m3u8_pos + 5;
        if (after == lower.length() ||
            lower[after] == '?' ||
            lower[after] == '#' ||
            lower[after] == '&' ||
            lower[after] == '/') {
            return true;
        }
    }

    // Check for manifest endpoints specifically indicating HLS
    if (lower.find("/master.m3u8") != std::string::npos ||
        lower.find("/playlist.m3u8") != std::string::npos ||
        lower.find("/manifest.m3u8") != std::string::npos) {
        return true;
    }

    // Check manifest URLs that explicitly specify HLS / m3u8 format
    if (lower.find("/manifest") != std::string::npos) {
        if (lower.find("format=m3u8") != std::string::npos ||
            lower.find("type=m3u8") != std::string::npos ||
            lower.find("format=hls") != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool isHlsMatch(const std::string& url, const std::string& mime, std::string* match_reason) {
    // Priority 1: MIME detection
    if (isHlsMimeType(mime)) {
        if (match_reason) *match_reason = "MIME: " + normalizeMimeType(mime);
        return true;
    }

    // Priority 2: URL fallback detection
    if (isHlsUrlFallback(url)) {
        if (match_reason) *match_reason = "URL fallback pattern";
        return true;
    }

    return false;
}

bool isTrackerOrAd(const std::string& url) {
    if (url.empty()) return false;
    std::string lower = toLower(url);

    static const char* const kBlockPatterns[] = {
        "doubleclick.net",
        "googlesyndication.com",
        "googleadservices.com",
        "adservice.google.",
        "facebook.net/tr",
        "facebook.com/tr",
        "google-analytics.com",
        "scorecardresearch.com",
        "criteo.net",
        "criteo.com",
        "hotjar.com",
        "adnxs.com",
        "rubiconproject.com",
        "/pixel?",
        "/pixel.gif",
        "/telemetry",
        "/analytics.js",
        "/gtag/js",
        nullptr
    };

    for (size_t i = 0; kBlockPatterns[i] != nullptr; ++i) {
        if (lower.find(kBlockPatterns[i]) != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool isExpensiveResource(const std::string& url) {
    if (url.empty()) return false;
    std::string lower = toLower(url);

    // Never block HLS or core web assets
    if (lower.find(".m3u8") != std::string::npos ||
        lower.find(".js") != std::string::npos ||
        lower.find(".html") != std::string::npos ||
        lower.find(".json") != std::string::npos) {
        return false;
    }

    // Check images
    static const char* const kImgExtensions[] = {
        ".png", ".jpg", ".jpeg", ".gif", ".webp", ".avif", ".svg", ".ico", nullptr
    };
    for (size_t i = 0; kImgExtensions[i] != nullptr; ++i) {
        size_t pos = lower.find(kImgExtensions[i]);
        if (pos != std::string::npos) {
            size_t end_pos = pos + std::string(kImgExtensions[i]).length();
            if (end_pos == lower.length() || lower[end_pos] == '?' || lower[end_pos] == '#') {
                return true;
            }
        }
    }

    // Check fonts
    static const char* const kFontExtensions[] = {
        ".woff", ".woff2", ".ttf", ".otf", ".eot", nullptr
    };
    for (size_t i = 0; kFontExtensions[i] != nullptr; ++i) {
        size_t pos = lower.find(kFontExtensions[i]);
        if (pos != std::string::npos) {
            size_t end_pos = pos + std::string(kFontExtensions[i]).length();
            if (end_pos == lower.length() || lower[end_pos] == '?' || lower[end_pos] == '#') {
                return true;
            }
        }
    }

    // Favicon paths
    if (lower.find("favicon.ico") != std::string::npos) {
        return true;
    }

    return false;
}

bool shouldBlockUrl(const std::string& url, bool fast_mode) {
    if (url.empty()) return false;

    // Obvious ads and trackers are blocked in all modes
    if (isTrackerOrAd(url)) {
        return true;
    }

    // In fast mode, aggressively block expensive images and fonts
    if (fast_mode && isExpensiveResource(url)) {
        return true;
    }

    return false;
}

std::string getContentFilterJson(bool fast_mode) {
    std::ostringstream json;
    json << "[\n";

    // Rule 1: Block known ad/tracking domains using native if-domain list
    json << "  {\n"
         << "    \"trigger\": {\n"
         << "      \"url-filter\": \".*\",\n"
         << "      \"if-domain\": [\"*doubleclick.net\", \"*googlesyndication.com\", \"*googleadservices.com\", \"*criteo.com\", \"*criteo.net\", \"*hotjar.com\", \"*adnxs.com\", \"*rubiconproject.com\", \"*scorecardresearch.com\", \"*google-analytics.com\"]\n"
         << "    },\n"
         << "    \"action\": {\n"
         << "      \"type\": \"block\"\n"
         << "    }\n"
         << "  }";

    if (fast_mode) {
        // Rule 2: Block image and font resource types natively
        json << ",\n"
             << "  {\n"
             << "    \"trigger\": {\n"
             << "      \"url-filter\": \".*\",\n"
             << "      \"resource-type\": [\"image\", \"font\"]\n"
             << "    },\n"
             << "    \"action\": {\n"
             << "      \"type\": \"block\"\n"
             << "    }\n"
             << "  }";
    }

    json << "\n]\n";
    return json.str();
}

std::string getDetectionJs() {
    return R"JAVASCRIPT(
(function() {
    'use strict';
    function notifyHls(url) {
        if (!url || typeof url !== 'string') return;
        if (url.indexOf('.m3u8') !== -1 || url.indexOf('mpegurl') !== -1) {
            try {
                if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.hlsDetector) {
                    window.webkit.messageHandlers.hlsDetector.postMessage(url);
                }
            } catch(e) {}
        }
    }

    // Hook window.fetch
    if (typeof window.fetch === 'function') {
        var origFetch = window.fetch;
        window.fetch = function(input, init) {
            var url = (typeof input === 'string') ? input : (input && input.url ? input.url : '');
            notifyHls(url);
            return origFetch.apply(this, arguments);
        };
    }

    // Hook XMLHttpRequest
    if (typeof XMLHttpRequest !== 'undefined' && XMLHttpRequest.prototype && XMLHttpRequest.prototype.open) {
        var origOpen = XMLHttpRequest.prototype.open;
        XMLHttpRequest.prototype.open = function(method, url) {
            notifyHls(url);
            return origOpen.apply(this, arguments);
        };
    }

    // Hook HTMLMediaElement (video and audio src)
    if (typeof HTMLMediaElement !== 'undefined' && HTMLMediaElement.prototype) {
        var origSrcDescriptor = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'src');
        if (origSrcDescriptor && origSrcDescriptor.set) {
            Object.defineProperty(HTMLMediaElement.prototype, 'src', {
                set: function(val) {
                    notifyHls(val);
                    return origSrcDescriptor.set.call(this, val);
                },
                get: function() {
                    return origSrcDescriptor.get.call(this);
                },
                configurable: true
            });
        }
    }
})();
)JAVASCRIPT";
}

} // namespace WpeExtractor
