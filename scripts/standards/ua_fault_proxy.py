"""Test-only loopback UA-TCP fault fixture, SecurityPolicy None, small unchunked requests.

Drops one connection at a specified ns0 binary service encoding, then forwards
new connections. It is not part of the production gateway or a general UA proxy.
"""
import select
import socket
import struct
import threading
import time


class SetupFaultProxy:
    def __init__(self, backend, service):
        self.backend, self.service = backend, service
        self.listener = socket.socket()
        self.listener.bind(('127.0.0.1', 0))
        self.port = self.listener.getsockname()[1]
        if len(str(backend)) != len(str(self.port)):
            self.listener.close()
            raise RuntimeError('fixture endpoint port widths must match')
        self.listener.listen()
        self.listener.settimeout(.1)
        self.done = threading.Event()
        self.lock = threading.Lock()
        self.pairs, self.threads, self.events, self.errors = [], [], [], []
        self.drops = self.connections = 0
        self.acceptor = threading.Thread(target=self._accept, daemon=True)
        self.acceptor.start()

    def _accept(self):
        while not self.done.is_set():
            try:
                client, _ = self.listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            try:
                server = socket.create_connection(('127.0.0.1', self.backend), timeout=1)
                server.settimeout(None)
            except OSError as error:
                client.close()
                with self.lock:
                    self.errors.append(str(error))
                continue
            worker = threading.Thread(target=self._bridge, args=(client, server), daemon=True)
            with self.lock:
                self.connections += 1
                self.pairs.append((client, server))
                self.threads.append(worker)
            worker.start()

    def _bridge(self, client, server):
        buffers = {client: b'', server: b''}
        source_url = f'opc.tcp://127.0.0.1:{self.backend}'.encode()
        proxy_url = f'opc.tcp://127.0.0.1:{self.port}'.encode()
        try:
            while not self.done.is_set():
                readable, _, _ = select.select([client, server], [], [], .1)
                for sock in readable:
                    block = sock.recv(65536)
                    if not block:
                        return
                    buffers[sock] += block
                    if len(buffers[sock]) > 524288:
                        raise ValueError('fixture buffer bound')
                    while len(buffers[sock]) >= 8:
                        size = struct.unpack_from('<I', buffers[sock], 4)[0]
                        if size < 8 or size > 262144:
                            raise ValueError('fixture message bound')
                        if len(buffers[sock]) < size:
                            break
                        frame, buffers[sock] = buffers[sock][:size], buffers[sock][size:]
                        # UA-TCP header + SecureChannel/token + SequenceHeader.
                        service = None
                        if frame[:3] == b'MSG' and len(frame) >= 28 and frame[24:26] == b'\x01\x00':
                            service = struct.unpack_from('<H', frame, 26)[0]
                        if sock is client and service is not None:
                            with self.lock:
                                if len(self.events) >= 4096:
                                    raise ValueError('fixture event bound')
                                self.events.append(service)
                                if service == self.service and self.drops == 0:
                                    self.drops = 1
                                    return
                        if sock is server:
                            # Only rewrite this controlled fixture's equal-width URL.
                            # This preserves UA String lengths and UA-TCP framing.
                            frame = frame.replace(source_url, proxy_url)
                        (server if sock is client else client).sendall(frame)
        except OSError:
            pass  # EOF/reset is expected during the one injected close and shutdown.
        except ValueError as error:
            with self.lock:
                self.errors.append(str(error))
        finally:
            client.close()
            server.close()

    def close(self):
        self.done.set()
        self.listener.close()
        self.acceptor.join(timeout=2)
        with self.lock:
            pairs, threads = list(self.pairs), list(self.threads)
        for pair in pairs:
            for sock in pair:
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                sock.close()
        deadline = time.monotonic() + 2
        for worker in threads:
            worker.join(timeout=max(0, deadline - time.monotonic()))
        closed = not self.acceptor.is_alive() and not any(worker.is_alive() for worker in threads)
        with self.lock:
            return dict(target_service=self.service, injected_drops=self.drops,
                        connections=self.connections, observed_services=list(self.events),
                        errors=list(self.errors), threads_closed=closed)
