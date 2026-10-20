// MIT. Private per-VM protocol; never registered with a system service.
package net.ryuya_dev.protectedlinux.display;
import android.view.Surface;
interface IOwnedDisplay {
    int getProtocolVersion();
    oneway void attach(String session, long generation, in Surface surface, boolean cursor);
    oneway void detach(String session, long generation, boolean cursor);
}
