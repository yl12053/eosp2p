package io.szktas.eos.Event;

import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.common.Mod;

import java.util.Objects;

import static io.szktas.eos.EOSBinder.EOSNative.SOCKET_NAME;
import static io.szktas.eos.Main.LOGGER;

@Mod.EventBusSubscriber
public class ConnectLogHandler {
    @SubscribeEvent
    public static void OnIncoming(ConnectIncoming event) {
        LOGGER.info("{} <- {}: Incoming Using Socket {}", event.SelfPUID, event.RemotePUID, event.SocketName);
        if (!Objects.equals(event.SocketName, SOCKET_NAME)) {
            LOGGER.warn("Dropping as socket name mismatch: {}", SOCKET_NAME);
        }
    }

    @SubscribeEvent
    public static void OnEstablished(ConnectEstablished event) {
        LOGGER.info("{} <- {}: Established Using Socket {}", event.SelfPUID, event.RemotePUID, event.SocketName);
        if (!Objects.equals(event.SocketName, SOCKET_NAME)) {
            LOGGER.warn("Dropping as socket name mismatch: {}", SOCKET_NAME);
        }
    }

    @SubscribeEvent
    public static void OnInterrupt(ConnectInterrupt event) {
        LOGGER.info("{} <- {}: Interrupt Using Socket {}", event.SelfPUID, event.RemotePUID, event.SocketName);
        if (!Objects.equals(event.SocketName, SOCKET_NAME)) {
            LOGGER.warn("Dropping as socket name mismatch: {}", SOCKET_NAME);
        }
    }

    @SubscribeEvent
    public static void OnClose(ConnectClose event) {
        LOGGER.info("{} <- {}: Close Using Socket {}", event.SelfPUID, event.RemotePUID, event.SocketName);
        if (!Objects.equals(event.SocketName, SOCKET_NAME)) {
            LOGGER.warn("Dropping as socket name mismatch: {}", SOCKET_NAME);
        }
    }
}
