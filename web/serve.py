#!/usr/bin/env python3
import argparse
import functools
import http.server
import os
import socketserver


def main():
    parser = argparse.ArgumentParser(description="Serve the EuGraph web console")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()

    root = os.path.dirname(os.path.abspath(__file__))
    os.chdir(root)

    handler = functools.partial(
        http.server.SimpleHTTPRequestHandler,
        directory=root,
    )

    class ReusableThreadingTCPServer(socketserver.ThreadingTCPServer):
        allow_reuse_address = True

    with ReusableThreadingTCPServer((args.host, args.port), handler) as httpd:
        print(f"Serving {root} on http://{args.host}:{args.port}")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
