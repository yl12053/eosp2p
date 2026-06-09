package io.szktas.eos.Event;

import io.szktas.eos.Client.Gui.HintGui;
import io.szktas.eos.EOSBinder.EOSNative;
import io.szktas.eos.EOSBinder.PacketConsumer;
import io.szktas.eos.Main;
import io.szktas.eos.Network.ConnectionKey;
import io.szktas.eos.Network.NetworkUtil;
import io.szktas.eos.Network.PacketHandler;
import io.szktas.eos.Util.ClientSocketNameSupplier;
import io.szktas.eos.Util.ServerSocketNameSupplier;
import net.minecraft.network.chat.Component;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.DistExecutor;
import net.minecraftforge.fml.common.Mod;
import net.minecraftforge.fml.event.lifecycle.FMLCommonSetupEvent;
import org.apache.commons.lang3.SystemUtils;

import javax.annotation.Nullable;
import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;

import static io.szktas.eos.Config.DEDICATED_SERVER_SECRET;
import static io.szktas.eos.EOSBinder.EOSNative.*;
import static io.szktas.eos.Main.LOGGER;
import static io.szktas.eos.Main.MODID;

@Mod.EventBusSubscriber(modid = Main.MODID, bus = Mod.EventBusSubscriber.Bus.MOD)
public class FMLCommonSetupHandler {
    @SubscribeEvent
    public static void OnCommonSetup(FMLCommonSetupEvent event) {
        LOGGER.debug("Setup");
        if (reason != null) {
            LOGGER.error(EOSNative.reason.name());
            return;
        };
        SOCKET_NAME = DistExecutor.safeRunForDist(
                () -> ClientSocketNameSupplier::new,
                () -> ServerSocketNameSupplier::new
        ).get();
        EOSNative.initConnectionHandle(() -> {
            LOGGER.info("Connection handle initialization success");
            SetNameGetter(() -> NAME_SUPPLIER.getName() + MODID);
            executor.scheduleAtFixedRate(new Runnable() {
                private int last = 0;
                private boolean shallSub = false;

                @Override
                public void run() {
                    int newStatus;
                    try (Socket socket = new Socket()) {
                        socket.connect(new InetSocketAddress("api.epicgames.dev", 443), 5000);
                        newStatus = 0;
                    } catch (IOException e) {
                        newStatus = 2;
                    }
                    if (newStatus != last) {
                        setNetworkStatus(newStatus);
                        last = newStatus;
                    }
                    if (newStatus == 0 && shallSub) {
                        subscribeIncomingConnectionRequest(PUID, SOCKET_NAME);
                    }
                    if (newStatus != 0) {
                        shallSub = false;
                    }
                }
            }, 0, 15, TimeUnit.SECONDS);
            EOSNative.getPUID((puid) -> {
                LOGGER.debug("Get PUID: {}", puid);
                subscribeIncomingConnectionRequestHandler((local, remote, name) ->
                        EXECUTOR.accept(() -> MinecraftForge.EVENT_BUS.post(new ConnectIncoming(local, remote, name))));
                if (!subscribeIncomingConnectionRequest(puid, null)) {
                    LOGGER.error("Failed to subscribe");
                    IsRunningEOS = false;
                    reasonEOS = ReasonEOS.SUBSCRIBE_INCOMING_CONNECTION_FAILED;
                    return;
                }
                subscribeEstablishedConnectionRequestHandler((local, remote, name) ->
                        EXECUTOR.accept(() -> MinecraftForge.EVENT_BUS.post(new ConnectEstablished(local, remote, name))));
                if (!subscribeEstablishedConnectionRequest(puid, null)) {
                    LOGGER.error("Failed to subscribe");
                    IsRunningEOS = false;
                    reasonEOS = ReasonEOS.SUBSCRIBE_ESTABLISHED_CONNECTION_FAILED;
                    return;
                }
                subscribeInterruptConnectionRequestHandler((local, remote, name) ->
                        EXECUTOR.accept(() -> MinecraftForge.EVENT_BUS.post(new ConnectInterrupt(local, remote, name))));
                if (!subscribeInterruptConnectionRequest(puid, null)) {
                    LOGGER.error("Failed to subscribe");
                    IsRunningEOS = false;
                    reasonEOS = ReasonEOS.SUBSCRIBE_INTERRUPT_CONNECTION_FAILED;
                    return;
                }
                subscribeCloseConnectionRequestHandler((local, remote, name) ->
                        EXECUTOR.accept(() -> MinecraftForge.EVENT_BUS.post(new ConnectClose(local, remote, name))));
                if (!subscribeCloseConnectionRequest(puid, null)) {
                    LOGGER.error("Failed to subscribe");
                    IsRunningEOS = false;
                    reasonEOS = ReasonEOS.SUBSCRIBE_CLOSE_CONNECTION_FAILED;
                    return;
                }
                registerReceiveCallbackFor(puid, (rid, sid, cid, data) -> {
                    ConnectionKey key = new ConnectionKey(rid, sid, cid);
                    @Nullable Set<PacketHandler> handlers = NetworkUtil.DATA_CALLBACKS.get(key);
                    if (handlers != null) {
                        for (PacketHandler handler: handlers) {
                            handler.accept(data);
                        }
                    }
                });
            }, () -> {
                IsRunningEOS = false;
                LOGGER.error("Get PUID Failed, EOS shutdown");
                if (SystemUtils.IS_OS_MAC) {
                    SET_ERROR_SCREEN.set(
                            Component.translatable("gui.eosp2p." + reasonEOS.name().toLowerCase()),
                            Component.translatable("gui.eosp2p.puid_fail_mac", Component.translatable("gui.eosp2p.puid_fail")),
                            Component.translatable("gui.eosp2p.dismiss_ever"),
                            Component.translatable("gui.eosp2p.dismiss")
                    );
                } else {
                    SET_ERROR_SCREEN.set(
                            Component.translatable("gui.eosp2p." + reasonEOS.name().toLowerCase()),
                            Component.translatable("gui.eosp2p.puid_fail"),
                            Component.translatable("gui.eosp2p.dismiss_ever"),
                            Component.translatable("gui.eosp2p.dismiss")
                    );
                }
            });
        }, (ret) -> {
            if (ret > 0) {
                LOGGER.error("Failed to initialize connection handle, reason: {}", EOSNative.reasonEOS.name());
            } else {
                LOGGER.error("Error here {} {}", EOSNative.IsEnabled, EOSNative.reasonEOS);
                if (!EOSNative.IsEnabled && EOSNative.reason != null) {
                    LOGGER.error(EOSNative.reason.name());
                }
                if (EOSNative.reasonEOS != null) {
                    LOGGER.error(EOSNative.reasonEOS.name());
                }
            }
        });
    }
}
