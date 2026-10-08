#ifndef WPE_URL_EXTRACTOR_EXTRACTOR_HPP
#define WPE_URL_EXTRACTOR_EXTRACTOR_HPP

#include "browser.hpp"
#include <string>

namespace WpeExtractor {

/**
 * Standard exit error codes matching Section 35:
 * 0 = HLS found
 * 1 = HLS not found
 * 2 = invalid URL
 * 3 = WPE initialization failure
 * 4 = navigation/network failure
 * 5 = timeout
 * 6 = internal error
 */
enum class ExtractionStatus {
    HLS_FOUND = 0,
    HLS_NOT_FOUND = 1,
    INVALID_URL = 2,
    WPE_INIT_FAILURE = 3,
    NETWORK_FAILURE = 4,
    TIMEOUT = 5,
    INTERNAL_ERROR = 6
};

struct ExtractionResult {
    ExtractionStatus status = ExtractionStatus::HLS_NOT_FOUND;
    std::string hls_url;
    std::string match_reason;
    std::string error_message;
    double startup_ms = 0.0;
    double detection_ms = 0.0;
    double total_ms = 0.0;

    int exitCode() const {
        return static_cast<int>(status);
    }
};

class HlsExtractor {
public:
    /**
     * Run a single HLS URL extraction pipeline:
     * Validates URL -> Initializes ephemeral WPE -> Navigates -> Intercepts -> Cleans up.
     */
    static ExtractionResult extract(const std::string& url, const BrowserOptions& options);
};

} // namespace WpeExtractor

#endif // WPE_URL_EXTRACTOR_EXTRACTOR_HPP
