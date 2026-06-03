package io.szktas.eos.Network;

import lombok.*;

import java.io.Serial;
import java.net.InetSocketAddress;
import java.net.SocketAddress;

@Getter @Setter @AllArgsConstructor @EqualsAndHashCode(callSuper = false) @ToString
public class EOSAddress extends SocketAddress {
    @Serial
    private static final long serialVersionUID = 1L;
    private final String PUID;
    private final String SocketID;
    private final byte ChannelID;

    public static class EOSInetAddress extends InetSocketAddress {
        @Serial
        private static final long serialVersionUID = 1L;
        public final String PUID;
        public final String SocketID;
        public final byte ChannelID;
        public EOSInetAddress(String PUID, String SocketID, byte ChannelID) {
            super(PUID + ":" + SocketID, ChannelID);
            this.PUID = PUID;
            this.SocketID = SocketID;
            this.ChannelID = ChannelID;
        }
    }
}
