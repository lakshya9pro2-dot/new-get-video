#include "extractor.hpp"
#include "filters.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace WpeExtractor {

static void printHelp(const char* prog) {
    std::cout << "Usage: " << prog << " <URL> [OPTIONS]\n"
              << "   or: " << prog << " --server [--port <port>]\n\n"
              << "Production-ready, ultra-fast headless HLS URL extractor using WPE WebKit.\n\n"
              << "Options:\n"
              << "  --timeout <ms>       Maximum navigation timeout in milliseconds (default: 200000)\n"
              << "  --fast               Enable aggressive optimizations (block images, fonts, trackers)\n"
              << "  --user-agent <str>   Custom HTTP User-Agent header\n"
              << "  --referer <str>      Custom HTTP Referer header\n"
              << "  --verbose            Print diagnostic logs and timings to stderr\n"
              << "  --server             Run in lightweight HTTP API server mode\n"
              << "  --port <port>        HTTP server listening port (default: 8080)\n"
              << "  -h, --help           Display this help message and exit\n\n"
              << "Exit Codes:\n"
              << "  0 = HLS found\n"
              << "  1 = HLS not found\n"
              << "  2 = Invalid URL\n"
              << "  3 = WPE initialization failure\n"
              << "  4 = Navigation/network failure\n"
              << "  5 = Timeout\n"
              << "  6 = Internal error\n";
}

static std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int h1 = std::tolower(in[i + 1]);
            int h2 = std::tolower(in[i + 2]);
            int v1 = (h1 >= '0' && h1 <= '9') ? (h1 - '0') : ((h1 >= 'a' && h1 <= 'f') ? (h1 - 'a' + 10) : -1);
            int v2 = (h2 >= '0' && h2 <= '9') ? (h2 - '0') : ((h2 >= 'a' && h2 <= 'f') ? (h2 - 'a' + 10) : -1);
            if (v1 >= 0 && v2 >= 0) {
                out += static_cast<char>((v1 << 4) | v2);
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out += ' ';
            continue;
        }
        out += in[i];
    }
    return out;
}

static void runHttpServer(int port, const BrowserOptions& default_options) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[ERROR] Failed to create socket: " << strerror(errno) << "\n";
        exit(static_cast<int>(ExtractionStatus::INTERNAL_ERROR));
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[ERROR] Failed to bind to port " << port << ": " << strerror(errno) << "\n";
        close(server_fd);
        exit(static_cast<int>(ExtractionStatus::INTERNAL_ERROR));
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "[ERROR] Listen failed: " << strerror(errno) << "\n";
        close(server_fd);
        exit(static_cast<int>(ExtractionStatus::INTERNAL_ERROR));
    }

    std::cout << "[INFO] WPE HLS Extractor HTTP Server listening on http://0.0.0.0:" << port << "\n";
    std::cout << "[INFO] Endpoint: GET /extract?url=<target_url>[&fast=1][&timeout=ms]\n";

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) continue;

        char buffer[4096];
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            close(client_fd);
            continue;
        }
        buffer[bytes_read] = '\0';

        std::string request(buffer);
        std::istringstream req_stream(request);
        std::string method, path;
        req_stream >> method >> path;

        auto sendResponse = [](int fd, const std::string& msg) {
            ssize_t total = 0;
            ssize_t len = static_cast<ssize_t>(msg.size());
            while (total < len) {
                ssize_t w = write(fd, msg.c_str() + total, len - total);
                if (w <= 0) break;
                total += w;
            }
        };

        // Health check endpoint
        if (path == "/health" || path == "/") {
            std::string body = "{\"status\": \"ok\", \"service\": \"wpe-url-extractor\"}\n";
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            sendResponse(client_fd, resp);
            close(client_fd);
            continue;
        }

        // Endpoint: /extract?url=...
        if (path.rfind("/extract", 0) != 0) {
            std::string body = "{\"error\": \"Not Found\"}\n";
            std::string resp = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            sendResponse(client_fd, resp);
            close(client_fd);
            continue;
        }

        // Check if an extraction is already in progress (Section 23: return 429)
        if (Browser::isExtractionActive()) {
            std::string body = "{\"success\": false, \"error\": \"Busy: extraction already in progress (single-instance limit)\"}\n";
            std::string resp = "HTTP/1.1 429 Too Many Requests\r\nContent-Type: application/json\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            sendResponse(client_fd, resp);
            close(client_fd);
            continue;
        }

        // Parse query parameters
        std::string target_url;
        BrowserOptions req_options = default_options;

        size_t q_pos = path.find('?');
        if (q_pos != std::string::npos) {
            std::string query = path.substr(q_pos + 1);
            std::istringstream q_stream(query);
            std::string param;
            while (std::getline(q_stream, param, '&')) {
                size_t eq_pos = param.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = param.substr(0, eq_pos);
                    std::string val = urlDecode(param.substr(eq_pos + 1));
                    if (key == "url") {
                        target_url = val;
                    } else if (key == "timeout") {
                        req_options.timeout_ms = static_cast<guint>(std::strtoul(val.c_str(), nullptr, 10));
                    } else if (key == "fast") {
                        req_options.fast_mode = (val == "1" || val == "true");
                    } else if (key == "referer") {
                        req_options.referer = val;
                    } else if (key == "user-agent") {
                        req_options.user_agent = val;
                    }
                }
            }
        }

        if (target_url.empty()) {
            std::string body = "{\"success\": false, \"error\": \"Missing url parameter\"}\n";
            std::string resp = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\nContent-Length: " +
                               std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            sendResponse(client_fd, resp);
            close(client_fd);
            continue;
        }

        // Run extraction on demand (ephemeral instance created and destroyed per request)
        ExtractionResult result = HlsExtractor::extract(target_url, req_options);

        std::ostringstream json;
        if (result.status == ExtractionStatus::HLS_FOUND) {
            json << "{\n"
                 << "  \"success\": true,\n"
                 << "  \"url\": \"" << result.hls_url << "\"\n"
                 << "}\n";
        } else {
            json << "{\n"
                 << "  \"success\": false,\n"
                 << "  \"url\": null\n"
                 << "}\n";
        }

        std::string body = json.str();
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                           std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        sendResponse(client_fd, resp);
        close(client_fd);
    }

    close(server_fd);
}

} // namespace WpeExtractor

int main(int argc, char* argv[]) {
    if (argc < 2) {
        WpeExtractor::printHelp(argv[0]);
        return static_cast<int>(WpeExtractor::ExtractionStatus::INVALID_URL);
    }

    std::string target_url;
    WpeExtractor::BrowserOptions options;
    bool server_mode = false;
    int server_port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            WpeExtractor::printHelp(argv[0]);
            return 0;
        } else if (arg == "--server") {
            server_mode = true;
        } else if (arg == "--port" && i + 1 < argc) {
            server_mode = true;
            server_port = std::atoi(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            options.timeout_ms = static_cast<guint>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--user-agent" && i + 1 < argc) {
            options.user_agent = argv[++i];
        } else if (arg == "--referer" && i + 1 < argc) {
            options.referer = argv[++i];
        } else if (arg == "--fast") {
            options.fast_mode = true;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg[0] != '-') {
            target_url = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            WpeExtractor::printHelp(argv[0]);
            return static_cast<int>(WpeExtractor::ExtractionStatus::INVALID_URL);
        }
    }

    if (server_mode) {
        WpeExtractor::runHttpServer(server_port, options);
        return 0;
    }

    if (target_url.empty()) {
        std::cerr << "Error: No target URL specified.\n";
        return static_cast<int>(WpeExtractor::ExtractionStatus::INVALID_URL);
    }

    // Run extraction
    WpeExtractor::ExtractionResult res = WpeExtractor::HlsExtractor::extract(target_url, options);

    if (res.status == WpeExtractor::ExtractionStatus::HLS_FOUND) {
        // Section 4 & 34: Normal successful output must be ONLY the URL
        std::cout << res.hls_url << "\n";
        return 0;
    } else {
        // Section 4: Failure output
        std::cout << "No HLS URL found\n";
        if (options.verbose && !res.error_message.empty()) {
            std::cerr << "[DEBUG] Error: " << res.error_message << "\n";
        }
        return res.exitCode();
    }
}
