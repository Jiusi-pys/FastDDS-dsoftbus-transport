package org.fastdds.standards.b5;

public final class DdsException extends RuntimeException {
    private static final long serialVersionUID = 1L;
    private final Status status;
    public DdsException(Status status) { super("DDS operation failed: " + status); this.status=status; }
    public Status status() { return status; }
}
