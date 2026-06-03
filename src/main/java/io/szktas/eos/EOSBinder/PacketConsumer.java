package io.szktas.eos.EOSBinder;

@FunctionalInterface
public interface PacketConsumer {
    void accept(String RemotePUID, String SocketID, byte ChannelID, byte[] Data);
}
