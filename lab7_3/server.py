from http.server import BaseHTTPRRequestHandler, HTTPServer
import logging

class MyRequestHandler(BaseHTTPRRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/html')
        self.end-headers()
        self.wfile.write(b"Hello! You made GET request.")

    def do_POST(self):
        content_length = int(self.headers['Content-Length'])
        post_data = self.rfile.read(content_length)
        logging.info(f"Received POST data: {post_data.decode('utf-8')}")

        self.send_response(200)
        self.send_header('Content-type', 'text/html')
        self.end_headers()
        self.wfile.write(b"POST request received. Thank you!")

def run (server_class=HTTPServer, handler_class=MyRequestHandler, port = 8000):
    logging.basicConfig(level=logging.INFO)
    server_address = ('', port)
    httpd = server_class(server_address, handler_class)
    logging.info(f"Starting server on port {port}...")
    httpd.serve_forever()

if __name__ == '__main__':
    run()
