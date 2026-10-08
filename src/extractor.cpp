#include "extractor.hpp"
#include "filters.hpp"
#include <iostream>

namespace WpeExtractor {

ExtractionResult HlsExtractor::extract(const std::string& url, const BrowserOptions& options) {
    ExtractionResult result;

    // 1. Validate URL (Section 21 & Section 35: code 2 = invalid URL)
    if (!isValidUrl(url)) {
        result.status = ExtractionStatus::INVALID_URL;
        result.error_message = "Invalid URL scheme or format. Only http:// and https:// are permitted.";
        return result;
    }

    // 2. Check concurrency (Section 23: Single active extraction only)
    if (Browser::isExtractionActive()) {
        result.status = ExtractionStatus::INTERNAL_ERROR;
        result.error_message = "An extraction is already in progress (single-instance limit).";
        return result;
    }

    // 3. Initialize ephemeral Browser instance (Section 1 & 22)
    Browser browser;
    if (!browser.init(options)) {
        const auto& state = browser.getState();
        result.status = ExtractionStatus::WPE_INIT_FAILURE;
        result.error_message = state.error_message.empty() ? "Failed to initialize WPE WebKit" : state.error_message;
        return result;
    }

    // 4. Run navigation and network interception
    bool found = browser.extract(url);
    const auto& state = browser.getState();

    result.startup_ms = state.startup_ms;
    result.detection_ms = state.detection_ms;
    result.total_ms = state.total_ms;
    result.match_reason = state.match_reason;

    if (found) {
        result.status = ExtractionStatus::HLS_FOUND;
        result.hls_url = state.hls_url;
    } else if (state.tls_failed) {
        result.status = ExtractionStatus::NETWORK_FAILURE;
        result.error_message = state.error_message;
    } else if (state.load_finished) {
        result.status = ExtractionStatus::HLS_NOT_FOUND;
        result.error_message = "Page loaded completely but no HLS stream was detected";
    } else if (state.timed_out) {
        result.status = ExtractionStatus::TIMEOUT;
        result.error_message = "Extraction timed out before finding any HLS URL";
    } else if (state.load_failed || !state.error_message.empty()) {
        result.status = ExtractionStatus::NETWORK_FAILURE;
        result.error_message = state.error_message;
    } else {
        result.status = ExtractionStatus::HLS_NOT_FOUND;
        result.error_message = "No HLS URL found";
    }

    // 5. Ephemeral context and WebView are destroyed here when browser goes out of scope
    return result;
}

} // namespace WpeExtractor
