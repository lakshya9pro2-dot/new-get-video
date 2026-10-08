import http.server
import socketserver
import urllib.parse
import sys

PORT = 9876

class HlsTestHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Keep test server logs clean
        pass

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path == "/test-js":
            # JS-generated HLS
            html = """<!DOCTYPE html>
            <html>
            <head><title>JS Test</title></head>
            <body>
            <h1>JS Generated HLS Test</h1>
            <script>
            setTimeout(() => {
                fetch('/media/stream.m3u8?token=abc1234').catch(e => {});
            }, 50);
            </script>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/test-xhr":
            # XHR-generated HLS
            html = """<!DOCTYPE html>
            <html>
            <head><title>XHR Test</title></head>
            <body>
            <script>
            var xhr = new XMLHttpRequest();
            xhr.open('GET', '/media/video_manifest.m3u8');
            xhr.send();
            </script>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/test-video-tag":
            # Native HTML5 video tag with HLS src
            html = """<!DOCTYPE html>
            <html>
            <body>
            <video src="/media/master.m3u8" controls></video>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/test-no-extension":
            # HLS without .m3u8 in URL, but with correct MIME
            html = """<!DOCTYPE html>
            <html>
            <body>
            <script>
            fetch('/live/stream-endpoint').catch(e=>{});
            </script>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/live/stream-endpoint":
            # M3U8 MIME without .m3u8 extension
            self.send_response(200)
            self.send_header("Content-Type", "application/vnd.apple.mpegurl; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"#EXTM3U\n#EXT-X-VERSION:3\n")

        elif path == "/test-bad-mime":
            # .m3u8 in URL but incorrect MIME (e.g. text/plain)
            html = """<!DOCTYPE html>
            <html>
            <body>
            <script>
            fetch('/media/fallback.m3u8').catch(e=>{});
            </script>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/media/fallback.m3u8":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(b"#EXTM3U\n#EXT-X-VERSION:3\n")

        elif path == "/test-redirect":
            html = """<!DOCTYPE html>
            <html>
            <body>
            <script>
            fetch('/redirect-gateway').catch(e=>{});
            </script>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/redirect-gateway":
            self.send_response(302)
            self.send_header("Location", "/media/final-playlist.m3u8")
            self.end_headers()

        elif path == "/test-referer":
            html = """<!DOCTYPE html>
            <html>
            <body>
            <script>
            fetch('/media/protected.m3u8').catch(e=>{});
            </script>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/media/protected.m3u8":
            ref = self.headers.get("Referer", "")
            if "my-custom-referer" in ref or "http" in ref:
                self.send_response(200)
                self.send_header("Content-Type", "application/x-mpegURL")
                self.end_headers()
                self.wfile.write(b"#EXTM3U\n")
            else:
                self.send_response(403)
                self.end_headers()

        elif path == "/test-fast":
            # Page with images and trackers, plus HLS
            html = """<!DOCTYPE html>
            <html>
            <body>
            <img src="/heavy-image.png">
            <script src="https://googleadservices.com/pagead/conversion.js"></script>
            <script>
            setTimeout(() => {
                fetch('/media/fast_stream.m3u8').catch(e=>{});
            }, 30);
            </script>
            </body>
            </html>"""
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif path == "/test-no-hls":
            # Normal page with NO HLS
            html = "<html><body><h1>Hello, no streams here!</h1></body></html>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.end_headers()
            self.wfile.write(html.encode('utf-8'))

        elif ".m3u8" in path:
            self.send_response(200)
            self.send_header("Content-Type", "application/vnd.apple.mpegurl")
            self.end_headers()
            self.wfile.write(b"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1280000\nlow.m3u8\n")

        else:
            self.send_response(404)
            self.end_headers()

if __name__ == "__main__":
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", PORT), HlsTestHandler) as httpd:
        print(f"Test server running on port {PORT}", flush=True)
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass
