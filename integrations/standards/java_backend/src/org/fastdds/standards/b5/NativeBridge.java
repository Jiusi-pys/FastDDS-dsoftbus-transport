package org.fastdds.standards.b5;

final class NativeBridge {
    private NativeBridge() {}
    static native String abi();
    static native String backend();
    static native long allocate();
    static native long liveOwners();
    static native int open(long handle,int domain,byte[] topic,int writer,int representation);
    static native int close(long handle);
    static native int waitMatched(long handle,int timeoutMs);
    static native int write(long handle,long id,long sequence,byte[] label,byte[] payload);
    static native BoundedRecord take(long handle,int[] status);
    static native int waitAcknowledged(long handle,int timeoutMs);
}
