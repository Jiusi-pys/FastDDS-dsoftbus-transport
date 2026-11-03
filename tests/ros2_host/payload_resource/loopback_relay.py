"""Bounded test-only mutation of captured user DATA_FRAG fields.

HostSocket's actual four-byte-length TCP envelope is preserved. Discovery and
authentication records are forwarded unchanged. No new protocol implementation,
external destination, packet generator, payload dump or product allocation guard.
Offsets follow fixed 2.14.7 DataMsg.hpp/MessageReceiver.cpp; unexpected layouts
fail the fixture rather than guessing. Raw RTPS bytes never enter Git/receipts.
"""
import socket
import struct
import threading


class Relay:
    def __init__(self, destination, scene, maximum):
        self.destination=destination;self.scene=scene;self.maximum=maximum
        self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(1)
        self.port=self.listener.getsockname()[1];self.closed=threading.Event()
        self.sockets=[];self.threads=[];self.errors=[];self.events=[];self.lock=threading.Lock()
        self.frames=0;self.bytes=0
        self.start(self.accept)

    def start(self, target, *args):
        worker=threading.Thread(target=target,args=args);self.threads.append(worker);worker.start()

    def accept(self):
        try:
            incoming,_=self.listener.accept();outgoing=socket.create_connection(('127.0.0.1',self.destination),timeout=10)
            incoming.settimeout(1);outgoing.settimeout(1);self.sockets.extend([incoming,outgoing])
            self.start(self.copy,incoming,outgoing,True);self.start(self.copy,outgoing,incoming,False)
        except OSError as error:
            if not self.closed.is_set():self.errors.append(str(error))

    def exact(self, stream, size):
        data=bytearray()
        while len(data)<size and not self.closed.is_set():
            try:chunk=stream.recv(size-len(data))
            except socket.timeout:continue
            if not chunk:return None
            data.extend(chunk)
        return bytes(data) if len(data)==size else None

    def mutate(self, data):
        if self.scene=='boundary' or data[:4]!=b'RTPS':return data
        result=bytearray(data[:20]);offset=20;submessages=0
        while offset<len(data):
            submessages+=1
            if submessages>2048 or offset+4>len(data):raise ValueError('fixture submessage bound/header n=%d off=%d len=%d head=%s'%(submessages,offset,len(data),data[max(0,offset-8):offset+12].hex()))
            kind,flags=data[offset:offset+2];endian='<' if flags&1 else '>'
            size=struct.unpack_from(endian+'H',data,offset+2)[0]
            end=offset+4+size if size else len(data)
            if end>len(data):raise ValueError('fixture submessage extent')
            part=data[offset:end]
            if kind==0x16:
                body=offset+4
                if size<32:raise ValueError('fixture DATA_FRAG extent')
                # User Writer entity kinds only; builtin/security readers stay untouched.
                if data[body+11] in (2,3):
                    inline=struct.unpack_from(endian+'H',data,body+2)[0]
                    first=struct.unpack_from(endian+'I',data,body+20)[0]
                    fragment=struct.unpack_from(endian+'H',data,body+26)[0]
                    sample=struct.unpack_from(endian+'I',data,body+28)[0]
                    if 70009<=sample<=70128:
                        if flags&2 or inline!=28 or fragment<1024:raise ValueError('fixture requires declared plain 2.14.7 user geometry')
                        if first==1:
                            payload=data[body+32:end]
                            if len(payload)<1024 or payload[8:9]!=b'H':raise ValueError('fixture CDR/string identity')
                            claimed=0x7fffffff if self.scene=='huge' else self.maximum
                            part=bytearray(part[:36]+payload[:1024])
                            struct.pack_into(endian+'H',part,2,1056)
                            struct.pack_into(endian+'H',part,28,1) # fragmentsInSubmessage
                            struct.pack_into(endian+'H',part,30,1024)
                            struct.pack_into(endian+'I',part,32,claimed)
                            with self.lock:
                                if len(self.events)<64:self.events.append(dict(kind='DATA_FRAG',fragment_size=1024,fragments=1,
                                    sample_size=claimed,wire_payload_bytes=1024,writer_entity=data[body+8:body+12].hex(),
                                    sequence=data[body+12:body+20].hex(),scope='CAPTURED_AUTHENTICATED_MATCHED_USER_WRITER'))
                        else:part=b'' # incomplete reassembly fixture, never completion/ACK synthesis.
            result.extend(part);offset=end
        return bytes(result)

    def copy(self, source, destination, forward):
        try:
            while not self.closed.is_set():
                header=self.exact(source,4)
                if header is None:break
                size=struct.unpack('!I',header)[0]
                if not 0<size<=65536:raise ValueError('fixture frame size')
                frame=self.exact(source,size)
                if frame is None:break
                with self.lock:
                    self.frames+=1;self.bytes+=size
                    if self.frames>4096 or self.bytes>16*1024*1024:raise ValueError('fixture traffic bound')
                frame=self.mutate(frame) if forward else frame
                destination.sendall(struct.pack('!I',len(frame))+frame)
        except (OSError,ValueError) as error:
            if not self.closed.is_set():self.errors.append(str(error))
        finally:
            try:destination.shutdown(socket.SHUT_WR)
            except OSError:pass

    def close(self):
        self.closed.set()
        for stream in [self.listener]+self.sockets:
            try:stream.shutdown(socket.SHUT_RDWR)
            except OSError:pass
            stream.close()
        for worker in self.threads:worker.join(3)
        if any(worker.is_alive() for worker in self.threads):self.errors.append('relay worker did not join')
