package io.szktas.eos.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.llamalad7.mixinextras.sugar.Local;
import io.netty.bootstrap.AbstractBootstrap;
import io.netty.bootstrap.Bootstrap;
import io.netty.channel.ChannelFuture;
import io.szktas.eos.EOSBinder.EOSNative;
import io.szktas.eos.Network.EOSAddress;
import io.szktas.eos.Network.EosP2PChannel;
import net.minecraft.network.Connection;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.util.concurrent.ConcurrentHashMap;

import static io.szktas.eos.EOSBinder.EOSNative.PUID;
import static io.szktas.eos.Main.LOGGER;

@Mixin(Connection.class)
public class MixinConnection {
    @Unique
    private final static ConcurrentHashMap<EOSAddress, EosP2PChannel> eosp2p$clientChannelMap = new ConcurrentHashMap<>();

    @Unique
    private final static ConcurrentHashMap<EOSAddress, ChannelFuture> eosp2p$waitingList = new ConcurrentHashMap<>();

    @WrapOperation(method = "connect", at = @At(value = "INVOKE", target = "Lio/netty/bootstrap/Bootstrap;channel(Ljava/lang/Class;)Lio/netty/bootstrap/AbstractBootstrap;", remap = false))
    private static AbstractBootstrap<?, ?> editChannel(Bootstrap instance, Class<?> aClass, Operation<AbstractBootstrap<?, ?>> original, @Local(argsOnly = true) InetSocketAddress addr) {
        if (!(addr instanceof EOSAddress.EOSInetAddress EOSAddressRaw && EOSNative.isCanUse())) {
            return original.call(instance, aClass);
        }
        EOSAddress Local = new EOSAddress(PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);
        EOSAddress Remote = new EOSAddress(EOSAddressRaw.PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);
        return instance.channelFactory(() -> new EosP2PChannel(null, Local, Remote));
    }

    @WrapOperation(method = "connect", at = @At(value = "INVOKE", target = "Lio/netty/bootstrap/Bootstrap;connect(Ljava/net/InetAddress;I)Lio/netty/channel/ChannelFuture;", remap = false))
    private static ChannelFuture editConnect(Bootstrap instance, InetAddress inetHost, int inetPort, Operation<ChannelFuture> original, @Local(argsOnly = true) InetSocketAddress addr) {
        if (!(addr instanceof EOSAddress.EOSInetAddress EOSAddressRaw && EOSNative.isCanUse())) {
            return original.call(instance, inetHost, inetPort);
        }
        EOSAddress Local = new EOSAddress(PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);
        EOSAddress Remote = new EOSAddress(EOSAddressRaw.PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);

        ChannelFuture future = instance.connect(Remote, Local);
        eosp2p$waitingList.put(Remote, future);

        future.addListener(f -> {
            LOGGER.debug("Resolve, success = {}", f.isSuccess());
            eosp2p$waitingList.remove(Remote);
            if (f.isSuccess()) {
                EosP2PChannel channel = (EosP2PChannel) f.get();
                eosp2p$clientChannelMap.put(Remote, channel);
                channel.closeFuture().addListener(p -> {
                    eosp2p$clientChannelMap.remove(Remote);
                });
            } else {
                LOGGER.debug("Cancelled/Failed, calling native to clean up");
                EOSNative.close(PUID, EOSAddressRaw.PUID, EOSAddressRaw.SocketID);
                eosp2p$clientChannelMap.remove(Remote);
            }
        });

        return future;
    }

    @WrapMethod(method = "connect")
    private static ChannelFuture wrapConnect(InetSocketAddress pAddress, boolean pUseEpollIfAvailable, Connection pConnection, Operation<ChannelFuture> original) {
        if (!(pAddress instanceof EOSAddress.EOSInetAddress EOSAddressRaw && EOSNative.isCanUse())) {
            return original.call(pAddress, pUseEpollIfAvailable, pConnection);
        }

        EOSAddress Remote = new EOSAddress(EOSAddressRaw.PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);
        EosP2PChannel chan = eosp2p$clientChannelMap.get(Remote);
        if (chan != null) {
            if (!chan.isOpen()) {
                LOGGER.debug("Try to kill connection");
                EOSNative.close(PUID, EOSAddressRaw.PUID, EOSAddressRaw.SocketID);
            }
            LOGGER.debug("Return existing con");
            return chan.newSucceededFuture();
        }

        ChannelFuture tryWaiting = eosp2p$waitingList.get(Remote);
        if (tryWaiting != null) {
            LOGGER.debug("Try to kill pre-alloc con");
            tryWaiting.cancel(true);
            LOGGER.debug("Try to kill connection");
            EOSNative.close(PUID, EOSAddressRaw.PUID, EOSAddressRaw.SocketID);
            eosp2p$waitingList.remove(Remote);
        }

        LOGGER.debug("Return new con");
        return original.call(pAddress, pUseEpollIfAvailable, pConnection);
    }
}
