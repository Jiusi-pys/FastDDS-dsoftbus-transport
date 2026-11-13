package org.fastdds.standards.b5;

import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.CharBuffer;
import java.util.Objects;

/** Explicit owner. Construct before opening, so even a partial failed open has a reachable close owner. */
public final class Endpoint implements AutoCloseable {
    public enum Role { READER, WRITER }
    private long handle;
    private boolean attempted, opened, closing;
    public Endpoint() { Library.requireLoaded();handle=NativeBridge.allocate();if(handle==0) throw new IllegalStateException("Native owner allocation failed"); }
    private void alive() { if(handle==0) throw new IllegalStateException("Endpoint is closed"); }
    private void usable() { alive();if(!opened||closing) throw new IllegalStateException("Endpoint is not open for operations"); }
    private static void timeout(int ms) { if(ms<0||ms>60000) throw new IllegalArgumentException("Timeout outside 0..60000ms"); }
    public synchronized Status open(int domain,String topic,Role role,int representation) {
        alive();if(attempted) throw new IllegalStateException("Open already attempted");
        Objects.requireNonNull(topic,"topic");Objects.requireNonNull(role,"role");
        if(domain<0||domain>232||(representation!=0&&representation!=2)||topic.indexOf(0)>=0)
            throw new IllegalArgumentException("Invalid endpoint options");
        byte[] encoded;
        try {
            var bytes=StandardCharsets.UTF_8.newEncoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).encode(CharBuffer.wrap(topic));
            encoded=new byte[bytes.remaining()];bytes.get(encoded);
        } catch(CharacterCodingException error) { throw new IllegalArgumentException("Malformed topic",error); }
        if(encoded.length==0||encoded.length>255) throw new IllegalArgumentException("Topic byte bound outside 1..255");
        attempted=true;closing=true;
        Status status=Status.fromNative(NativeBridge.open(handle,domain,encoded,role==Role.WRITER?1:0,representation));
        opened=status==Status.OK;closing=!opened;return status;
    }
    public synchronized Status waitMatched(int timeoutMs) { usable();timeout(timeoutMs);return Status.fromNative(NativeBridge.waitMatched(handle,timeoutMs)); }
    public synchronized Status write(BoundedRecord sample) {
        usable();Objects.requireNonNull(sample,"sample");
        return Status.fromNative(NativeBridge.write(handle,sample.id(),sample.sequenceNumber(),sample.label(),sample.payload()));
    }
    public synchronized SampleResult take() {
        usable();int[] status={Status.ERROR.ordinal()};BoundedRecord value=NativeBridge.take(handle,status);
        return new SampleResult(Status.fromNative(status[0]),value);
    }
    public synchronized Status waitAcknowledged(int timeoutMs) { usable();timeout(timeoutMs);return Status.fromNative(NativeBridge.waitAcknowledged(handle,timeoutMs)); }
    public synchronized Status tryClose() {
        if(handle==0) return Status.OK;
        closing=true;Status result=Status.fromNative(NativeBridge.close(handle));
        if(result==Status.OK){handle=0;opened=false;}return result;
    }
    @Override public synchronized void close() { Status result=tryClose();if(result!=Status.OK) throw new DdsException(result); }
}
