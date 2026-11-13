package org.fastdds.standards.b5;

import java.nio.file.Path;
import java.nio.file.Files;
import java.util.Arrays;

/** Independent Java consumer of the SDK; the other process is the existing B3 public-DDS oracle. */
public final class Client {
    private Client() {}
    private static void need(boolean value,String description) {if(!value)throw new AssertionError(description);}
    private static BoundedRecord sample(int n) {
        byte[] label=new byte[n==0?0:64],payload=new byte[n==0?0:256];
        Arrays.fill(label,(byte)'z');Arrays.fill(payload,(byte)255);
        return new BoundedRecord(17+n,42+n,label,payload);
    }
    public static void main(String[] args) throws Exception {
        Library.load(Path.of(args[0]),args[1]);String role=args[2];
        need(role.equals("writer")||role.equals("reader"),"role");
        int domain=Integer.parseInt(args[3]),rep=Integer.parseInt(args[5]);
        System.out.println("B5_START pid="+ProcessHandle.current().pid()+" abi="+Library.abi()+" backend=host-udp");System.out.flush();
        try(var endpoint=new Endpoint()) {
            need(endpoint.open(domain,args[4],role.equals("writer")?Endpoint.Role.WRITER:Endpoint.Role.READER,rep)==Status.OK,"open");
            System.out.println("B5_READY role="+role);System.out.flush();
            if(args.length==7) {
                Path gate=Path.of(args[6]);long readyDeadline=System.nanoTime()+10_000_000_000L;
                while(!Files.isRegularFile(gate)&&System.nanoTime()<readyDeadline)Thread.sleep(10);
                need(Files.isRegularFile(gate),"trusted launcher gate deadline");
            }
            need(endpoint.waitMatched(15000)==Status.OK,"match");
            if(role.equals("writer")) {
                for(int n=0;n<10;++n)need(endpoint.write(sample(n))==Status.OK,"write "+n);
                need(endpoint.waitAcknowledged(5000)==Status.OK,"ACK");
            } else {
                boolean[] seen=new boolean[10];int count=0;long end=System.nanoTime()+15_000_000_000L;
                while(count<10&&System.nanoTime()<end) {
                    SampleResult result=endpoint.take();
                    if(result.status()==Status.NO_DATA){Thread.sleep(10);continue;}
                    need(result.status()==Status.OK,"take");
                    int n=(int)result.sample().id()-17;need(n>=0&&n<10&&sample(n).equals(result.sample()),"sample values");
                    need(!seen[n],"duplicate actual sample");seen[n]=true;++count;
                }
                need(count==10,"ten exact samples");
            }
        }
        need(Library.liveEndpoints()==0,"native owners drained");
        System.out.println("B5_DDS PASS role="+role+" abi="+args[1]+" rep="+rep+" samples=10 owners=0");
    }
}
