#!/usr/bin/env python3
"""Local Three.js frontend backed by the production C++ game (no ROM serving)."""
import argparse
import gzip
import socket
import json
import math
import re
from pathlib import Path
import subprocess
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

class Bridge:
    def __init__(self):
        self.lock = threading.Lock()
        self.log = (ROOT / 'build/threejs_bridge.log').open('w')
        self.start()

    def start(self):
        self.commands = []
        self.last_frame = 0
        self.log.flush()
        self.log_start = self.log.tell()
        self.process = subprocess.Popen([str(ROOT / 'build/threejs_bridge')], cwd=ROOT,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=self.log, text=True, bufsize=1)
        if self.process.stdout.readline().strip() != '{"ready":true}':
            self.close()
            raise RuntimeError('Native game could not load; see build/threejs_bridge.log')

    def stopped_error(self):
        # Save physical inputs only on failure, keeping disk I/O out of play.
        (ROOT / 'build/threejs_failure_replay.txt').write_text('\n'.join(self.commands) + '\n')
        self.log.flush()
        with (ROOT / 'build/threejs_bridge.log').open() as log:
            log.seek(self.log_start)
            diagnostic = log.read()[-4096:]
        matches = re.findall(r'unported caller instruction at PC (\d+)', diagnostic)
        location = f' at ROM PC ${int(matches[-1]):04X}' if matches else ''
        return RuntimeError(f'Native game stopped{location} after frame {self.last_frame}; '
                            'inputs saved in build/threejs_failure_replay.txt')

    def request(self, request):
        action = request.get('action', 'tick')
        if action not in ('tick', 'demo', 'restart'):
            raise ValueError('Unknown action')
        count = request.get('count', 1)
        if type(count) is not int or not 1 <= count <= 781:
            raise ValueError('Frame count must be 1..781')
        if action == 'tick':
            axes = [request.get(k, 0) for k in ('x', 'y', 'throttle')]
            if any(type(v) not in (int, float) or not math.isfinite(v) for v in axes):
                raise ValueError('Axes must be finite numbers')
            buttons = [int(bool(request.get(k, False))) for k in ('cannon', 'missile', 'coin', 'start')]
            line = ' '.join(map(str, [action, count, *axes, *buttons, int(bool(request.get('fullScene', False))), int(bool(request.get('verify', False)))]))
        else:
            line = action if action == 'restart' else f'{action} {count} {int(bool(request.get("verify", False)))}'
        with self.lock:
            if action == 'restart':
                self.stop()
                self.start()
            if self.process.poll() is not None:
                raise self.stopped_error()
            self.commands.append(line)
            try:
                self.process.stdin.write(line + '\n')
                self.process.stdin.flush()
            except BrokenPipeError:
                raise self.stopped_error() from None
            response = self.process.stdout.readline()
            if not response:
                raise self.stopped_error()
            frame = json.loads(response)  # Reject accidental protocol diagnostics.
            self.last_frame = frame.get('frame', self.last_frame)
            return response.encode()

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()

    def close(self):
        self.stop()
        self.log.close()

class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def send(self, status, data, mime):
        compressed = mime == 'application/json' and 'gzip' in self.headers.get('Accept-Encoding', '')
        if compressed:
            data = gzip.compress(data, compresslevel=1, mtime=0)
        self.send_response(status)
        if compressed:
            self.send_header('Content-Encoding', 'gzip')
            self.send_header('Vary', 'Accept-Encoding')
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        paths = {
            '/': ('index.html', 'text/html; charset=utf-8'),
            '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
            '/scene.js': ('scene.js', 'text/javascript; charset=utf-8'),
            '/style.css': ('style.css', 'text/css; charset=utf-8'),
            '/vendor/three.module.js': ('node_modules/three/build/three.module.js', 'text/javascript'),
            '/vendor/three.core.js': ('node_modules/three/build/three.core.js', 'text/javascript'),
        }
        entry = paths.get(self.path.split('?', 1)[0])
        if entry is None:
            self.send(404, b'Not found', 'text/plain'); return
        path, mime = entry
        try:
            self.send(200, (HERE / path).read_bytes(), mime)
        except FileNotFoundError:
            self.send(404, b'Install dependencies with npm install in prototypes/threejs', 'text/plain')

    def do_POST(self):
        if self.path != '/api/frame':
            self.send(404, b'Not found', 'text/plain'); return
        origin = self.headers.get('Origin')
        if origin and origin != f'http://{self.headers.get("Host")}':
            self.send(403, b'Origin rejected', 'text/plain'); return
        try:
            length = int(self.headers.get('Content-Length', 0))
            if not 0 < length <= 4096:
                raise ValueError('Invalid request size')
            request = json.loads(self.rfile.read(length))
            if not isinstance(request, dict):
                raise ValueError('Expected an object')
            self.send(200, self.server.bridge.request(request), 'application/json')
        except (ValueError, RuntimeError, BrokenPipeError) as error:
            self.send(400, json.dumps({'error': str(error)}).encode(), 'application/json')

    def log_message(self, *_):
        pass

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    bridge = Bridge()
    try:
        server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
        server.bridge = bridge
        print(f'Three.js prototype: http://127.0.0.1:{args.port} (Ctrl-C to stop)', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
    finally:
        bridge.close()

if __name__ == '__main__':
    main()
