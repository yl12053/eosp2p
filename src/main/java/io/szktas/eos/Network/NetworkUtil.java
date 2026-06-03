package io.szktas.eos.Network;

import java.util.Collections;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public class NetworkUtil {
    public static ConcurrentHashMap<ConnectionKey, Set<PacketHandler>> DATA_CALLBACKS = new ConcurrentHashMap<>();

    public static void RegisterCallback(String remotePUID, String SocketID, byte ChannelID, PacketHandler handler) {
        ConnectionKey connectionKey = new ConnectionKey(remotePUID, SocketID, ChannelID);

        DATA_CALLBACKS.computeIfAbsent(
                connectionKey,
                (ignored) -> Collections.newSetFromMap(new ConcurrentHashMap<>())
        ).add(handler);
    }

    public static void DeleteCallback(String remotePUID, String SocketID, byte ChannelID, PacketHandler handler) {
        ConnectionKey connectionKey = new ConnectionKey(remotePUID, SocketID, ChannelID);

        DATA_CALLBACKS.computeIfAbsent(
                connectionKey,
                (ignored) -> Collections.newSetFromMap(new ConcurrentHashMap<>())
        ).remove(handler);
    }
}
