package org.fastdds.standards.b5;

public record SampleResult(Status status,BoundedRecord sample) {
    public SampleResult {
        if(status==null||(status==Status.OK)!=(sample!=null)) throw new IllegalArgumentException("Inconsistent sample result");
    }
}
