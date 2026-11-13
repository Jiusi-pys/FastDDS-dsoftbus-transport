package org.fastdds.standards.b5;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Objects;

/** A copied B4 fixture value. label is an IDL string8 byte sequence, not JNI modified UTF-8. */
public final class BoundedRecord {
    private final long id, sequenceNumber;
    private final byte[] label, payload;
    public BoundedRecord(long id, long sequenceNumber, byte[] label, byte[] payload) {
        Objects.requireNonNull(label,"label"); Objects.requireNonNull(payload,"payload");
        if(id<0||id>0xffff_ffffL||sequenceNumber<0||sequenceNumber>0xffff_ffffL)
            throw new IllegalArgumentException("uint32 outside range");
        if(label.length>64||payload.length>256) throw new IllegalArgumentException("Fixture bound exceeded");
        for(byte value:label) if(value==0) throw new IllegalArgumentException("Embedded NUL label");
        this.id=id;this.sequenceNumber=sequenceNumber;this.label=label.clone();this.payload=payload.clone();
    }
    public static byte[] latin1(String label) {
        Objects.requireNonNull(label,"label");
        if(label.length()>64) throw new IllegalArgumentException("Label bound exceeded");
        for(int i=0;i<label.length();++i) if(label.charAt(i)==0||label.charAt(i)>255)
            throw new IllegalArgumentException("Label is not non-NUL Latin-1");
        return label.getBytes(StandardCharsets.ISO_8859_1);
    }
    public long id() { return id; }
    public long sequenceNumber() { return sequenceNumber; }
    public byte[] label() { return label.clone(); }
    public byte[] payload() { return payload.clone(); }
    public String labelLatin1() { return new String(label,StandardCharsets.ISO_8859_1); }
    @Override public boolean equals(Object other) {
        if(!(other instanceof BoundedRecord value)) return false;
        return id==value.id&&sequenceNumber==value.sequenceNumber&&Arrays.equals(label,value.label)&&Arrays.equals(payload,value.payload);
    }
    @Override public int hashCode() { return Objects.hash(id,sequenceNumber,Arrays.hashCode(label),Arrays.hashCode(payload)); }
}
