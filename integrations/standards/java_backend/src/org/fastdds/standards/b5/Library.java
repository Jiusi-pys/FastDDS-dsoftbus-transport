package org.fastdds.standards.b5;

import java.io.IOException;
import java.nio.file.Path;
import java.util.Objects;

public final class Library {
    private static boolean attempted, loaded;
    private static Path path;
    private static String abi;
    private Library() {}
    /** One exact ABI per process, through the system class loader. A failed native load requires a new JVM. */
    public static synchronized void load(Path library,String expectedAbi) throws IOException {
        Objects.requireNonNull(library,"library");
        if(!"2.14.5".equals(expectedAbi)&&!"3.6.2".equals(expectedAbi)) throw new IllegalArgumentException("Unsupported ABI");
        if(!System.getProperty("os.name").startsWith("Windows")||!"64".equals(System.getProperty("sun.arch.data.model")))
            throw new UnsupportedOperationException("This milestone targets Windows x64");
        if(Library.class.getClassLoader()!=ClassLoader.getSystemClassLoader()) throw new UnsupportedOperationException("System class loader required");
        Path canonical=library.toRealPath();
        if(loaded&&canonical.equals(path)&&expectedAbi.equals(abi)) return;
        if(attempted) throw new IllegalStateException("Native library already attempted in this process");
        attempted=true;System.load(canonical.toString());
        if(!expectedAbi.equals(NativeBridge.abi())||!"host-udp".equals(NativeBridge.backend()))
            throw new UnsatisfiedLinkError("Native ABI/backend mismatch");
        path=canonical;abi=expectedAbi;loaded=true;
    }
    static synchronized void requireLoaded() { if(!loaded) throw new IllegalStateException("Load the exact native ABI first"); }
    public static synchronized String abi() { requireLoaded();return abi; }
    public static long liveEndpoints() { requireLoaded();return NativeBridge.liveOwners(); }
}
