#include "browser.hpp"
#include "filters.hpp"
#include <iostream>

namespace WpeExtractor {

std::atomic<bool> Browser::s_is_active{false};

Browser::Browser() {
}

Browser::~Browser() {
    cleanup();
}

void Browser::cleanup() {
    if (m_timeout_id > 0) {
        g_source_remove(m_timeout_id);
        m_timeout_id = 0;
    }

    if (m_idle_timeout_id > 0) {
        g_source_remove(m_idle_timeout_id);
        m_idle_timeout_id = 0;
    }

    if (m_web_view) {
        g_signal_handlers_disconnect_by_data(m_web_view, this);
        webkit_web_view_stop_loading(m_web_view);
        g_object_unref(m_web_view);
        m_web_view = nullptr;
    }

    if (m_ucm) {
        g_signal_handlers_disconnect_by_data(m_ucm, this);
        g_object_unref(m_ucm);
        m_ucm = nullptr;
    }

    if (m_settings) {
        g_object_unref(m_settings);
        m_settings = nullptr;
    }

    if (m_session) {
        g_object_unref(m_session);
        m_session = nullptr;
    }

    if (m_display) {
        g_object_unref(m_display);
        m_display = nullptr;
    }

    if (m_loop) {
        g_main_loop_unref(m_loop);
        m_loop = nullptr;
    }
}

void Browser::setupContentFilters() {
    if (!m_ucm) return;

    // Fast-path in-page JS hook for immediate JS-generated HLS detection
    std::string js = getDetectionJs();
    WebKitUserScript* script = webkit_user_script_new(
        js.c_str(),
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr
    );
    webkit_user_content_manager_add_script(m_ucm, script);
    webkit_user_script_unref(script);

    // Register script message handler
    g_signal_connect(m_ucm, "script-message-received::hlsDetector",
                     G_CALLBACK(onScriptMessageReceived), this);
    webkit_user_content_manager_register_script_message_handler(m_ucm, "hlsDetector", nullptr);

    // If fast mode, compile native WebKit content blocker rules
    if (m_options.fast_mode) {
        std::string filter_json = getContentFilterJson(true);
        GBytes* bytes = g_bytes_new_static(filter_json.c_str(), filter_json.size());

        // Ephemeral in-memory/local store for content filters
        g_mkdir_with_parents(".wpe_filter_store", 0700);
        WebKitUserContentFilterStore* store = webkit_user_content_filter_store_new(".wpe_filter_store");
        webkit_user_content_filter_store_save(
            store, "fast_filter", bytes, nullptr,
            [](GObject* source, GAsyncResult* res, gpointer user_data) {
                GError* error = nullptr;
                WebKitUserContentFilterStore* s = WEBKIT_USER_CONTENT_FILTER_STORE(source);
                WebKitUserContentFilter* filter = webkit_user_content_filter_store_save_finish(s, res, &error);
                if (filter && user_data) {
                    Browser* self = static_cast<Browser*>(user_data);
                    if (self->m_ucm) {
                        webkit_user_content_manager_add_filter(self->m_ucm, filter);
                    }
                    webkit_user_content_filter_unref(filter);
                } else if (error) {
                    g_error_free(error);
                }
            },
            this
        );
        g_bytes_unref(bytes);
        g_object_unref(store);
    }
}

bool Browser::init(const BrowserOptions& options) {
    m_options = options;
    m_t_start = std::chrono::steady_clock::now();

    if (m_options.verbose) {
        std::cerr << "[INFO] Initializing WPE WebKit headless platform...\n";
    }

    // 1. Create Headless Platform Display (WPE 2.54+)
    m_display = wpe_display_headless_new();
    if (!m_display) {
        m_state.error_message = "Failed to create WPE headless display";
        return false;
    }

    // 2. Ephemeral Network Session (zero disk storage, in-memory private browsing)
    m_session = webkit_network_session_new_ephemeral();
    if (!m_session) {
        m_state.error_message = "Failed to create ephemeral network session";
        return false;
    }

    webkit_network_session_set_persistent_credential_storage_enabled(m_session, FALSE);
    webkit_network_session_set_tls_errors_policy(m_session, WEBKIT_TLS_ERRORS_POLICY_FAIL);

    // Disable favicons
    WebKitWebsiteDataManager* dm = webkit_network_session_get_website_data_manager(m_session);
    if (dm) {
        webkit_website_data_manager_set_favicons_enabled(dm, FALSE);
    }

    // Set WebContext cache model to document viewer (disables network disk cache completely)
    WebKitWebContext* context = webkit_web_context_get_default();
    if (context) {
        webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
    }

    // 3. WebKit Settings
    m_settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(m_settings, TRUE);
    webkit_settings_set_enable_html5_database(m_settings, FALSE);
    webkit_settings_set_enable_media_stream(m_settings, FALSE);
    webkit_settings_set_enable_webaudio(m_settings, FALSE);
    webkit_settings_set_enable_developer_extras(m_settings, FALSE);
    webkit_settings_set_user_agent(m_settings, m_options.user_agent.c_str());

    if (m_options.fast_mode) {
        webkit_settings_set_auto_load_images(m_settings, FALSE);
    }

    // 4. User Content Manager and filters
    m_ucm = webkit_user_content_manager_new();
    setupContentFilters();

    // 5. Create WebKitWebView
    m_web_view = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "display", m_display,
        "network-session", m_session,
        "settings", m_settings,
        "user-content-manager", m_ucm,
        nullptr
    ));

    if (!m_web_view) {
        m_state.error_message = "Failed to create WebKitWebView";
        return false;
    }

    // Connect core callbacks
    g_signal_connect(m_web_view, "resource-load-started", G_CALLBACK(onResourceLoadStarted), this);
    g_signal_connect(m_web_view, "decide-policy", G_CALLBACK(onDecidePolicy), this);
    g_signal_connect(m_web_view, "load-changed", G_CALLBACK(onLoadChanged), this);
    g_signal_connect(m_web_view, "load-failed", G_CALLBACK(onLoadFailed), this);
    g_signal_connect(m_web_view, "load-failed-with-tls-errors", G_CALLBACK(onLoadFailedWithTlsErrors), this);
    g_signal_connect(m_web_view, "web-process-terminated", G_CALLBACK(onWebProcessTerminated), this);

    m_loop = g_main_loop_new(nullptr, FALSE);

    auto t_init_end = std::chrono::steady_clock::now();
    m_state.startup_ms = std::chrono::duration<double, std::milli>(t_init_end - m_t_start).count();

    if (m_options.verbose) {
        std::cerr << "[INFO] WPE initialized in " << m_state.startup_ms << " ms\n";
    }

    return true;
}

void Browser::onHlsDetected(const std::string& url, const std::string& reason) {
    if (m_state.found_hls) return; // Already detected

    // Resolve relative URL to absolute URL if needed
    std::string resolved = resolveUrl(m_base_url, url);

    m_state.found_hls = true;
    m_state.hls_url = resolved;
    m_state.match_reason = reason;

    if (m_idle_timeout_id > 0) {
        g_source_remove(m_idle_timeout_id);
        m_idle_timeout_id = 0;
    }

    if (m_timeout_id > 0) {
        g_source_remove(m_timeout_id);
        m_timeout_id = 0;
    }

    auto now = std::chrono::steady_clock::now();
    m_state.detection_ms = std::chrono::duration<double, std::milli>(now - m_t_nav_start).count();

    if (m_options.verbose) {
        std::cerr << "[INFO] HLS response detected!\n"
                  << "[INFO] Reason: " << reason << "\n"
                  << "[INFO] URL: " << resolved << "\n"
                  << "[INFO] Detection time: " << m_state.detection_ms << " ms\n"
                  << "[INFO] Stopping navigation and tearing down context immediately\n";
    }

    // Immediately stop loading to prevent any further network traffic or playlist downloading
    if (m_web_view) {
        webkit_web_view_stop_loading(m_web_view);
    }

    if (m_loop && g_main_loop_is_running(m_loop)) {
        g_main_loop_quit(m_loop);
    }
}

bool Browser::extract(const std::string& url) {
    // Single active extraction enforcement (Section 23)
    bool expected = false;
    if (!s_is_active.compare_exchange_strong(expected, true)) {
        m_state.error_message = "Another extraction is currently active";
        return false;
    }

    m_base_url = url;
    m_t_nav_start = std::chrono::steady_clock::now();

    if (m_options.verbose) {
        std::cerr << "[INFO] Loading URL: " << url << "\n";
    }

    // Setup configurable timeout
    m_timeout_id = g_timeout_add(m_options.timeout_ms, onTimeoutFired, this);

    // Build URI Request with optional Referer
    WebKitURIRequest* request = webkit_uri_request_new(url.c_str());
    if (!m_options.referer.empty()) {
        SoupMessageHeaders* headers = webkit_uri_request_get_http_headers(request);
        if (headers) {
            soup_message_headers_append(headers, "Referer", m_options.referer.c_str());
        }
    }

    webkit_web_view_load_request(m_web_view, request);
    g_object_unref(request);

    // Run the non-polling GLib event loop
    g_main_loop_run(m_loop);

    auto t_done = std::chrono::steady_clock::now();
    m_state.total_ms = std::chrono::duration<double, std::milli>(t_done - m_t_start).count();

    // Release single-instance lock
    s_is_active.store(false);

    return m_state.found_hls;
}

void Browser::onResourceLoadStarted(WebKitWebView* view, WebKitWebResource* resource,
                                    WebKitURIRequest* request, gpointer user_data) {
    (void)view;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self || self->m_state.found_hls) return;

    const char* uri = webkit_uri_request_get_uri(request);
    if (!uri) return;

    std::string s_uri(uri);

    if (self->m_options.verbose) {
        std::cerr << "[DEBUG] Request: " << s_uri << "\n";
    }

    // Check if the resource URL itself immediately matches HLS
    std::string reason;
    if (isHlsMatch(s_uri, "", &reason)) {
        self->onHlsDetected(s_uri, reason);
        return;
    }

    // If an idle timeout is ticking, extend it because network activity is still occurring
    if (self->m_idle_timeout_id > 0) {
        g_source_remove(self->m_idle_timeout_id);
        self->m_idle_timeout_id = g_timeout_add(8000, onIdleTimeoutFired, self);
    }

    // Connect notify::response to inspect response headers as soon as they arrive
    g_signal_connect(resource, "notify::response", G_CALLBACK(onResourceResponseNotify), self);
    g_signal_connect(resource, "sent-request", G_CALLBACK(onResourceSentRequest), self);
}

void Browser::onResourceResponseNotify(GObject* object, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self || self->m_state.found_hls) return;

    WebKitWebResource* resource = WEBKIT_WEB_RESOURCE(object);
    WebKitURIResponse* response = webkit_web_resource_get_response(resource);
    if (!response) return;

    const char* uri = webkit_uri_response_get_uri(response);
    const char* mime = webkit_uri_response_get_mime_type(response);

    std::string s_uri = uri ? uri : "";
    std::string s_mime = mime ? mime : "";

    // Also check raw Content-Type header from response
    SoupMessageHeaders* headers = webkit_uri_response_get_http_headers(response);
    if (headers && s_mime.empty()) {
        const char* ct = soup_message_headers_get_content_type(headers, nullptr);
        if (ct) s_mime = ct;
    }

    std::string reason;
    if (isHlsMatch(s_uri, s_mime, &reason)) {
        self->onHlsDetected(s_uri, reason);
    }
}

void Browser::onResourceSentRequest(WebKitWebResource* resource, WebKitURIRequest* request,
                                    WebKitURIResponse* redirected_response, gpointer user_data) {
    (void)resource;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self || self->m_state.found_hls) return;

    if (redirected_response) {
        const char* uri = webkit_uri_response_get_uri(redirected_response);
        const char* mime = webkit_uri_response_get_mime_type(redirected_response);
        std::string s_uri = uri ? uri : "";
        std::string s_mime = mime ? mime : "";

        std::string reason;
        if (isHlsMatch(s_uri, s_mime, &reason)) {
            self->onHlsDetected(s_uri, reason);
            return;
        }
    }

    const char* next_uri = webkit_uri_request_get_uri(request);
    if (next_uri) {
        std::string s_next(next_uri);
        std::string reason;
        if (isHlsMatch(s_next, "", &reason)) {
            self->onHlsDetected(s_next, reason);
        }
    }
}

gboolean Browser::onDecidePolicy(WebKitWebView* view, WebKitPolicyDecision* decision,
                                 WebKitPolicyDecisionType type, gpointer user_data) {
    (void)view;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self) return FALSE;

    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitResponsePolicyDecision* rpd = WEBKIT_RESPONSE_POLICY_DECISION(decision);
        WebKitURIResponse* response = webkit_response_policy_decision_get_response(rpd);
        if (response) {
            const char* uri = webkit_uri_response_get_uri(response);
            const char* mime = webkit_uri_response_get_mime_type(response);

            std::string s_uri = uri ? uri : "";
            std::string s_mime = mime ? mime : "";

            std::string reason;
            if (isHlsMatch(s_uri, s_mime, &reason)) {
                // Ignore the decision to cancel downloading playlist body (Section 9)
                webkit_policy_decision_ignore(decision);
                self->onHlsDetected(s_uri, reason);
                return TRUE;
            }
        }
    }

    return FALSE; // Let default handling proceed
}

void Browser::onScriptMessageReceived(WebKitUserContentManager* ucm, JSCValue* value,
                                      gpointer user_data) {
    (void)ucm;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self || self->m_state.found_hls) return;

    if (value && jsc_value_is_string(value)) {
        char* str = jsc_value_to_string(value);
        if (str) {
            std::string detected(str);
            g_free(str);

            if (self->m_options.verbose) {
                std::cerr << "[DEBUG] JS message received: " << detected << "\n";
            }

            std::string reason;
            if (isHlsMatch(detected, "", &reason)) {
                self->onHlsDetected(detected, "JS Interception: " + reason);
            }
        }
    }
}

gboolean Browser::onLoadFailed(WebKitWebView* view, WebKitLoadEvent load_event,
                               const gchar* failing_uri, GError* error, gpointer user_data) {
    (void)view;
    (void)load_event;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self || self->m_state.found_hls) return FALSE;

    // In fast mode, blocked resources trigger error - ignore them
    if (self->m_options.fast_mode && failing_uri && shouldBlockUrl(failing_uri, true)) {
        return TRUE;
    }

    if (error && self->m_options.verbose) {
        std::cerr << "[DEBUG] Load failed for " << (failing_uri ? failing_uri : "unknown")
                  << ": " << error->message << "\n";
    }

    return FALSE;
}

gboolean Browser::onLoadFailedWithTlsErrors(WebKitWebView* view, const gchar* failing_uri,
                                           GTlsCertificate* cert, GTlsCertificateFlags errors,
                                           gpointer user_data) {
    (void)view;
    (void)cert;
    (void)errors;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self) return FALSE;

    self->m_state.tls_failed = true;
    self->m_state.error_message = "TLS verification failed";
    if (self->m_options.verbose) {
        std::cerr << "[ERROR] TLS verification error for " << (failing_uri ? failing_uri : "unknown") << "\n";
    }

    if (self->m_loop && g_main_loop_is_running(self->m_loop)) {
        g_main_loop_quit(self->m_loop);
    }
    return TRUE; // Stop load
}

void Browser::onWebProcessTerminated(WebKitWebView* view, WebKitWebProcessTerminationReason reason,
                                     gpointer user_data) {
    (void)view;
    (void)reason;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self) return;

    if (!self->m_state.found_hls) {
        self->m_state.error_message = "WebProcess terminated unexpectedly";
    }

    if (self->m_loop && g_main_loop_is_running(self->m_loop)) {
        g_main_loop_quit(self->m_loop);
    }
}

void Browser::onLoadChanged(WebKitWebView* view, WebKitLoadEvent load_event, gpointer user_data) {
    (void)view;
    Browser* self = static_cast<Browser*>(user_data);
    if (!self || self->m_state.found_hls) return;

    if (load_event == WEBKIT_LOAD_FINISHED) {
        // Document finished loading. Allow grace period (up to 15000ms) for async JS/player bootstrapping.
        if (self->m_idle_timeout_id == 0) {
            guint grace = std::min(15000u, self->m_options.timeout_ms);
            self->m_idle_timeout_id = g_timeout_add(grace, onIdleTimeoutFired, self);
        }
    }
}

gboolean Browser::onIdleTimeoutFired(gpointer user_data) {
    Browser* self = static_cast<Browser*>(user_data);
    if (!self) return G_SOURCE_REMOVE;
    self->m_idle_timeout_id = 0;

    if (!self->m_state.found_hls) {
        self->m_state.load_finished = true;
        if (self->m_options.verbose) {
            std::cerr << "[INFO] Page load completed and grace period elapsed without detecting HLS\n";
        }
        if (self->m_loop && g_main_loop_is_running(self->m_loop)) {
            g_main_loop_quit(self->m_loop);
        }
    }
    return G_SOURCE_REMOVE;
}

gboolean Browser::onTimeoutFired(gpointer user_data) {
    Browser* self = static_cast<Browser*>(user_data);
    if (!self) return G_SOURCE_REMOVE;

    self->m_timeout_id = 0;
    self->m_state.timed_out = true;
    if (self->m_options.verbose) {
        std::cerr << "[INFO] Extraction timed out after " << self->m_options.timeout_ms << " ms\n";
    }

    if (self->m_loop && g_main_loop_is_running(self->m_loop)) {
        g_main_loop_quit(self->m_loop);
    }

    return G_SOURCE_REMOVE;
}

} // namespace WpeExtractor
