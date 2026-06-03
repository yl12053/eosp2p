package io.szktas.eos.Client;

import io.szktas.eos.Network.EOSAddress;
import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import net.minecraft.FieldsAreNonnullByDefault;
import net.minecraft.client.multiplayer.resolver.ResolvedServerAddress;

import javax.annotation.Nonnull;
import java.net.InetSocketAddress;

@Getter @Setter @AllArgsConstructor @ToString @FieldsAreNonnullByDefault
public class EOSResolveAddress implements ResolvedServerAddress {
    private final String remotePUID;
    private final String remoteSocketID;
    private final byte channelID;

    @Override @Nonnull
    public String getHostName() {
        return remotePUID + ":" + remoteSocketID;
    }

    @Override @Nonnull
    public String getHostIp() {
        return getHostName();
    }

    @Override
    public int getPort() {
        return channelID;
    }

    @Override @Nonnull
    public InetSocketAddress asInetSocketAddress() {
        return new EOSAddress.EOSInetAddress(remotePUID, remoteSocketID, channelID);
    }
}
