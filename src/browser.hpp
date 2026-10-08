#ifndef WPE_URL_EXTRACTOR_BROWSER_HPP
#define WPE_URL_EXTRACTOR_BROWSER_HPP

#include <string>
#include <chrono>
#include <atomic>
#include <glib.h>
#include <wpe/webkit.h>
#include <wpe/headless/wpe-headless.h>

namespace WpeExtractor {

// Single centralized default User-Agent as required by Section 17
inline constexpr const char* DEFAULT_USER_AGENT =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/134.0.0.0 Safari/537.36";

struct BrowserOptions {
    std::string user_agent = DEFAULT_USER_AGENT;
    std::string referer;
    guint timeout_ms = 200000; // Default 200000 ms, fully configurable via CLI
    bool fast_mode = false;
    bool verbose = false;
};

struct BrowserState {
    bool found_hls = false;
    std::string hls_url;
    std::string match_reason;
    bool timed_out = false;
    bool load_failed = false;
    bool load_finished = false;
    bool tls_failed = false;
    std::string error_message;
    double startup_ms = 0.0;
    double detection_ms = 0.0;
    double total_ms = 0.0;
};

class Browser {
public:
    Browser();
    ~Browser();

    // Prevent copy and assignment
    Browser(const Browser&) = delete;
    Browser& operator=(const Browser&) = delete;

    /**
     * Initialize WPE headless display, ephemeral session, settings, and view.
     * Returns true on success, false on initialization failure.
     */
    bool init(const BrowserOptions& options);

    /**
     * Start navigation to the target URL and enter the event loop.
     * Returns true if HLS was found, false otherwise.
     */
    bool extract(const std::string& url);

    /**
     * Get the final extraction state and metrics.
     */
    const BrowserState& getState() const { return m_state; }

    /**
     * Check if another extraction is currently active.
     */
    static bool isExtractionActive() { return s_is_active.load(); }

private:
    void cleanup();
    void onHlsDetected(const std::string& url, const std::string& reason);
    void setupContentFilters();

    // Static callbacks for GLib / WebKit signals
    static void onResourceLoadStarted(WebKitWebView* view, WebKitWebResource* resource,
                                      WebKitURIRequest* request, gpointer user_data);
    static void onResourceResponseNotify(GObject* object, GParamSpec* pspec, gpointer user_data);
    static void onResourceSentRequest(WebKitWebResource* resource, WebKitURIRequest* request,
                                      WebKitURIResponse* redirected_response, gpointer user_data);
    static gboolean onDecidePolicy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                   WebKitPolicyDecisionType type, gpointer user_data);
    static void onScriptMessageReceived(WebKitUserContentManager* ucm, JSCValue* value,
                                        gpointer user_data);
    static void onLoadChanged(WebKitWebView* view, WebKitLoadEvent load_event, gpointer user_data);
    static gboolean onLoadFailed(WebKitWebView* view, WebKitLoadEvent load_event,
                                 const gchar* failing_uri, GError* error, gpointer user_data);
    static gboolean onLoadFailedWithTlsErrors(WebKitWebView* view, const gchar* failing_uri,
                                             GTlsCertificate* cert, GTlsCertificateFlags errors,
                                             gpointer user_data);
    static void onWebProcessTerminated(WebKitWebView* view, WebKitWebProcessTerminationReason reason,
                                       gpointer user_data);
    static gboolean onTimeoutFired(gpointer user_data);
    static gboolean onIdleTimeoutFired(gpointer user_data);

    BrowserOptions m_options;
    BrowserState m_state;
    std::string m_base_url;

    WPEDisplay* m_display = nullptr;
    WebKitNetworkSession* m_session = nullptr;
    WebKitUserContentManager* m_ucm = nullptr;
    WebKitSettings* m_settings = nullptr;
    WebKitWebView* m_web_view = nullptr;
    GMainLoop* m_loop = nullptr;
    guint m_timeout_id = 0;
    guint m_idle_timeout_id = 0;

    std::chrono::steady_clock::time_point m_t_start;
    std::chrono::steady_clock::time_point m_t_nav_start;

    static std::atomic<bool> s_is_active;
};

} // namespace WpeExtractor

#endif // WPE_URL_EXTRACTOR_BROWSER_HPP
