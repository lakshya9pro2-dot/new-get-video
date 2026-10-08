#ifndef WPE_URL_EXTRACTOR_FILTERS_HPP
#define WPE_URL_EXTRACTOR_FILTERS_HPP

#include <string>
#include <vector>

namespace WpeExtractor {

/**
 * Validate that the input URL has an allowed scheme (http or https).
 * Strictly rejects file://, data://, javascript:, unix:, about:, etc.
 */
bool isValidUrl(const std::string& url);

/**
 * Resolve a relative URL (e.g. /media/stream.m3u8) against a base URL.
 */
std::string resolveUrl(const std::string& base_url, const std::string& relative_url);

/**
 * Normalize a MIME type string:
 * - Converts to lower case
 * - Strips leading/trailing whitespace
 * - Strips parameters after semicolon (e.g. "; charset=utf-8")
 */
std::string normalizeMimeType(const std::string& raw_mime);

/**
 * Primary HLS MIME detector:
 * Checks whether the MIME type is an HLS playlist type:
 * - application/vnd.apple.mpegurl
 * - application/x-mpegurl
 * - audio/mpegurl
 * - audio/x-mpegurl
 * - application/mpegurl
 */
bool isHlsMimeType(const std::string& raw_mime);

/**
 * Secondary HLS URL detector (fast fallback when server returns wrong MIME):
 * Checks whether the URL indicates an HLS stream:
 * - Ends with .m3u8
 * - Contains .m3u8? or .m3u8&
 * - Contains /master.m3u8 or /playlist.m3u8 or /manifest.m3u8
 * - Contains /manifest with format=m3u8 or type=hls
 * Conservative: never matches ordinary media files (.mp4, .ts, .jpg, .png).
 */
bool isHlsUrlFallback(const std::string& raw_url);

/**
 * Combined HLS match tester.
 * Returns true if MIME matches primary criteria OR URL matches fallback.
 * Populates reason string if non-null.
 */
bool isHlsMatch(const std::string& url, const std::string& mime, std::string* match_reason = nullptr);

/**
 * Check if the URL points to known advertising or tracking services.
 * Uses a tiny built-in blocklist of obvious tracking domains.
 */
bool isTrackerOrAd(const std::string& url);

/**
 * Check if the URL is an expensive static resource (images, fonts, favicons).
 * Strictly preserves HTML, JavaScript, XHR/fetch endpoints, JSON, APIs, media, m3u8.
 */
bool isExpensiveResource(const std::string& url);

/**
 * General URL filter decision:
 * Blocks trackers/ads, and in fast mode also blocks images/fonts.
 */
bool shouldBlockUrl(const std::string& url, bool fast_mode);

/**
 * Generate WebKit Content Blocker Rule JSON string.
 * Used with WebKitUserContentFilterStore for native engine-level resource blocking.
 */
std::string getContentFilterJson(bool fast_mode);

/**
 * Generate lightweight JavaScript injection for document start.
 * Hooks window.fetch, XMLHttpRequest, and HTMLMediaElement.src to detect
 * JavaScript-generated HLS URLs immediately without waiting for downloads.
 */
std::string getDetectionJs();

} // namespace WpeExtractor

#endif // WPE_URL_EXTRACTOR_FILTERS_HPP
