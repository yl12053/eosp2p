package io.szktas.eos.Network;

@FunctionalInterface
public interface PacketHandler {
    void accept(byte[] data);
}