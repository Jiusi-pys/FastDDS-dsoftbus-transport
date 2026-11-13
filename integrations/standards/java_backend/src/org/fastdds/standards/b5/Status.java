package org.fastdds.standards.b5;

public enum Status {
    OK, BAD_PARAMETER, BOUNDS, NO_DATA, TIMEOUT, ERROR, UNSUPPORTED;
    static Status fromNative(int code) {
        return switch(code) {
            case 0 -> OK; case 1 -> BAD_PARAMETER; case 2 -> BOUNDS;
            case 3 -> NO_DATA; case 4 -> TIMEOUT; case 5 -> ERROR;
            case 6 -> UNSUPPORTED;
            default -> throw new IllegalStateException("Unknown native status: " + code);
        };
    }
}
