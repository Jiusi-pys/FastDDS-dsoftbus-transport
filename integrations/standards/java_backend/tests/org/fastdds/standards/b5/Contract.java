package org.fastdds.standards.b5;

import java.nio.file.Path;
import java.util.Arrays;
import java.util.UUID;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class Contract {
    private static final AtomicInteger checks=new AtomicInteger();
    private Contract() {}
    private static void check(boolean condition) {checks.incrementAndGet();if(!condition)throw new AssertionError("Check " + checks.get());}
    private static void rejects(Class<? extends Throwable> type,Runnable action) {
        try {action.run();}catch(Throwable error){check(type.isInstance(error));return;}
        throw new AssertionError("Expected " + type.getName());
    }
    private static void record() {
        byte[] label=BoundedRecord.latin1("\u00e9\u00ff"),payload={(byte)255,0,17};
        var value=new BoundedRecord(0xffff_ffffL,0x8000_0000L,label,payload);
        label[0]=1;payload[0]=2;
        check(value.id()==0xffff_ffffL&&value.sequenceNumber()==0x8000_0000L);
        check(value.labelLatin1().equals("\u00e9\u00ff")&&value.payload()[0]==(byte)255);
        value.label()[0]=3;value.payload()[0]=4;check(value.label()[0]==(byte)233&&value.payload()[0]==(byte)255);
        check(new BoundedRecord(0,0,new byte[0],new byte[0]).label().length==0);
        byte[] full=new byte[64];Arrays.fill(full,(byte)255);check(new BoundedRecord(1,2,full,new byte[256]).label().length==64);
        rejects(IllegalArgumentException.class,()->new BoundedRecord(-1,0,new byte[0],new byte[0]));
        rejects(IllegalArgumentException.class,()->new BoundedRecord(0,0x1_0000_0000L,new byte[0],new byte[0]));
        rejects(IllegalArgumentException.class,()->new BoundedRecord(0,0,new byte[65],new byte[0]));
        rejects(IllegalArgumentException.class,()->new BoundedRecord(0,0,new byte[0],new byte[257]));
        rejects(IllegalArgumentException.class,()->new BoundedRecord(0,0,new byte[]{0},new byte[0]));
        rejects(NullPointerException.class,()->new BoundedRecord(0,0,null,new byte[0]));
        rejects(IllegalArgumentException.class,()->BoundedRecord.latin1("\u4e2d"));
        rejects(IllegalArgumentException.class,()->BoundedRecord.latin1("a\0b"));
        rejects(IllegalArgumentException.class,()->BoundedRecord.latin1("\ud800"));
        rejects(IllegalArgumentException.class,()->new SampleResult(Status.OK,null));
        check(new SampleResult(Status.NO_DATA,null).sample()==null);
    }
    private static void nativeContract(int domain) throws Exception {
        check(NativeBridge.close(0)==1&&NativeBridge.close(Long.MAX_VALUE)==1);
        check(NativeBridge.waitMatched(-1,0)==1&&NativeBridge.write(-1,0,0,null,null)==1);
        int[] status={0};check(NativeBridge.take(-1,status)==null&&status[0]==1);
        rejects(IllegalArgumentException.class,()->NativeBridge.take(-1,new int[0]));
        var unused=new Endpoint();check(unused.tryClose()==Status.OK&&unused.tryClose()==Status.OK);
        rejects(IllegalStateException.class,()->unused.waitMatched(0));
        String topic="B5_local_"+UUID.randomUUID().toString().replace("-","");
        try(var writer=new Endpoint();var reader=new Endpoint()) {
            rejects(IllegalArgumentException.class,()->writer.open(-1,topic,Endpoint.Role.WRITER,0));
            rejects(IllegalArgumentException.class,()->writer.open(domain,"a\0b",Endpoint.Role.WRITER,0));
            rejects(IllegalArgumentException.class,()->writer.open(domain,"\ud800",Endpoint.Role.WRITER,0));
            check(writer.open(domain,topic,Endpoint.Role.WRITER,0)==Status.OK);
            check(writer.waitMatched(0)==Status.TIMEOUT);
            rejects(IllegalStateException.class,()->writer.open(domain,topic,Endpoint.Role.WRITER,0));
            check(reader.open(domain,topic,Endpoint.Role.READER,0)==Status.OK);
            check(writer.waitMatched(15000)==Status.OK&&reader.waitMatched(15000)==Status.OK);
            check(reader.take().status()==Status.NO_DATA);
            byte[] label=new byte[64],payload=new byte[256];Arrays.fill(label,(byte)233);
            for(int i=0;i<256;++i)payload[i]=(byte)i;
            var input=new BoundedRecord(0xffff_ffffL,0x8000_0000L,label,payload);
            check(reader.write(input)==Status.BAD_PARAMETER&&reader.waitAcknowledged(0)==Status.BAD_PARAMETER);
            check(writer.take().status()==Status.BAD_PARAMETER);
            check(writer.write(input)==Status.OK&&writer.waitAcknowledged(5000)==Status.OK);
            long end=System.nanoTime()+5_000_000_000L;SampleResult result;
            do {result=reader.take();if(result.status()==Status.NO_DATA)Thread.sleep(10);}while(result.status()==Status.NO_DATA&&System.nanoTime()<end);
            check(result.status()==Status.OK&&input.equals(result.sample()));
            rejects(IllegalArgumentException.class,()->writer.waitMatched(60001));
            rejects(IllegalArgumentException.class,()->reader.waitAcknowledged(-1));
        }
        check(Library.liveEndpoints()==0);
        long id=NativeBridge.allocate();
        check(NativeBridge.open(id,-1,new byte[]{65},1,0)==1);
        check(NativeBridge.open(id,domain,new byte[]{0},1,0)==1);
        check(NativeBridge.open(id,domain,new byte[256],1,0)==2);
        check(NativeBridge.waitMatched(id,-1)==1&&NativeBridge.waitAcknowledged(id,60001)==1);
        check(NativeBridge.write(id,-1,0,new byte[0],new byte[0])==2);
        check(NativeBridge.write(id,0,0,new byte[65],new byte[0])==2);
        check(NativeBridge.write(id,0,0,new byte[]{0},new byte[0])==1);
        check(NativeBridge.close(id)==0&&NativeBridge.close(id)==1);
        check(Library.liveEndpoints()==0);
    }
    private static void concurrency(int domain) throws Exception {
        AtomicReference<Throwable> failure=new AtomicReference<>();Thread[] threads=new Thread[8];
        for(int i=0;i<threads.length;++i) {
            threads[i]=new Thread(()->{try {for(int n=0;n<100;++n){long id=NativeBridge.allocate();check(id>0);check(NativeBridge.close(id)==0);check(NativeBridge.waitMatched(id,0)==1);}}
                catch(Throwable error){failure.compareAndSet(null,error);}});threads[i].start();
        }
        for(Thread thread:threads)thread.join();if(failure.get()!=null)throw new AssertionError(failure.get());
        long id=NativeBridge.allocate();String topic="B5_race_"+UUID.randomUUID().toString().replace("-","");
        check(NativeBridge.open(id,domain,BoundedRecord.latin1(topic),0,0)==0);
        CyclicBarrier start=new CyclicBarrier(2);AtomicInteger waited=new AtomicInteger(-1),closed=new AtomicInteger(-1);
        Thread wait=new Thread(()->{try{start.await();waited.set(NativeBridge.waitMatched(id,100));}catch(Throwable error){failure.set(error);}});
        Thread close=new Thread(()->{try{start.await();closed.set(NativeBridge.close(id));}catch(Throwable error){failure.set(error);}});
        wait.start();close.start();wait.join();close.join();if(failure.get()!=null)throw new AssertionError(failure.get());
        check(closed.get()==0&&(waited.get()==1||waited.get()==4));check(Library.liveEndpoints()==0);
    }
    public static void main(String[] args) throws Exception {
        String mode=args[0],abi=args[2];int domain=Integer.parseInt(args[3]);
        if(mode.equals("record"))record();
        else if(mode.equals("wrong-abi")||mode.equals("wrong-backend")) {
            try {Library.load(Path.of(args[1]),mode.equals("wrong-abi")?(abi.equals("2.14.5")?"3.6.2":"2.14.5"):abi);throw new AssertionError("Wrong library accepted");}
            catch(UnsatisfiedLinkError expected){check(true);}
            rejects(IllegalStateException.class,Endpoint::new);
        } else {
            Library.load(Path.of(args[1]),abi);check(Library.abi().equals(abi));
            Library.load(Path.of(args[1]),abi);
            if(mode.equals("native"))nativeContract(domain);
            else if(mode.equals("concurrency"))concurrency(domain);
            else throw new IllegalArgumentException("Unknown contract mode");
        }
        System.out.println("B5_CONTRACT PASS mode="+mode+" checks="+checks.get());
    }
}
