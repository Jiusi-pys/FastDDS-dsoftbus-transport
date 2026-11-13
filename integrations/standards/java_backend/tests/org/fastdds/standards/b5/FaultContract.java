package org.fastdds.standards.b5;

import java.nio.file.Path;

/** This exercises only the synthetic native owner fault path, not DDS or a physical Socket failure. */
public final class FaultContract {
    private FaultContract() {}
    private static void need(boolean condition){if(!condition)throw new AssertionError("Synthetic ownership contract");}
    public static void main(String[] args) throws Exception {
        System.load(Path.of(args[0]).toRealPath().toString());
        need(NativeBridge.backend().equals("synthetic-close")&&NativeBridge.abi().equals(args[1]));
        for(String topic:new String[]{"open-failure","close-failure","throw-failure"}) {
            System.out.println("B5_SYNTHETIC_CASE "+topic);
            long id=NativeBridge.allocate();int opened=NativeBridge.open(id,219,BoundedRecord.latin1(topic),1,0);
            need(opened==(topic.equals("open-failure")?5:0));
            if(topic.equals("open-failure"))need(NativeBridge.waitMatched(id,0)==5);
            if(topic.equals("throw-failure")) {
                try{NativeBridge.close(id);throw new AssertionError("Exception escaped or missing");}
                catch(IllegalStateException expected){need(true);}
            } else need(NativeBridge.close(id)==5);
            need(NativeBridge.liveOwners()==1&&NativeBridge.waitAcknowledged(id,0)==5);
            need(NativeBridge.write(id,1,2,new byte[0],new byte[0])==5);
            need(NativeBridge.close(id)==0&&NativeBridge.liveOwners()==0&&NativeBridge.close(id)==1);
        }
        System.out.println("B5_SYNTHETIC_OWNERSHIP PASS cases=3 owners=0");
    }
}
