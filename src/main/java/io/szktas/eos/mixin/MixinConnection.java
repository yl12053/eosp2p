package io.szktas.eos.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.llamalad7.mixinextras.sugar.Local;
import io.netty.bootstrap.AbstractBootstrap;
import io.netty.bootstrap.Bootstrap;
import io.netty.channel.ChannelFuture;
import io.szktas.eos.Network.EOSAddress;
import io.szktas.eos.Network.EosP2PChannel;
import net.minecraft.network.Connection;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

import java.net.InetAddress;
import java.net.InetSocketAddress;

import static io.szktas.eos.EOSBinder.EOSNative.PUID;
import static io.szktas.eos.Main.LOGGER;

@Mixin(Connection.class)
public class MixinConnection {
    @WrapOperation(method = "connect", at = @At(value = "INVOKE", target = "Lio/netty/bootstrap/Bootstrap;channel(Ljava/lang/Class;)Lio/netty/bootstrap/AbstractBootstrap;", remap = false))
    private static AbstractBootstrap<?, ?> editChannel(Bootstrap instance, Class<?> aClass, Operation<AbstractBootstrap<?, ?>> original, @Local(argsOnly = true) InetSocketAddress addr) {
        if (!(addr instanceof EOSAddress.EOSInetAddress EOSAddressRaw)) {
            return original.call(instance, aClass);
        }
        EOSAddress Local = new EOSAddress(PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);
        EOSAddress Remote = new EOSAddress(EOSAddressRaw.PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);
        return instance.channelFactory(() -> new EosP2PChannel(null, Local, Remote));
    }

    @WrapOperation(method = "connect", at = @At(value = "INVOKE", target = "Lio/netty/bootstrap/Bootstrap;connect(Ljava/net/InetAddress;I)Lio/netty/channel/ChannelFuture;", remap = false))
    private static ChannelFuture editConnect(Bootstrap instance, InetAddress inetHost, int inetPort, Operation<ChannelFuture> original, @Local(argsOnly = true) InetSocketAddress addr) {
        if (!(addr instanceof EOSAddress.EOSInetAddress EOSAddressRaw)) {
            return original.call(instance, inetHost, inetPort);
        }
        EOSAddress Local = new EOSAddress(PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);
        EOSAddress Remote = new EOSAddress(EOSAddressRaw.PUID, EOSAddressRaw.SocketID, EOSAddressRaw.ChannelID);

        ChannelFuture future = instance.connect(Remote, Local);

        future.addListener(f -> {
            if (!f.isSuccess()) {
                LOGGER.error("Bootstrap Connect failed: ", f.cause());
            }
        });

        return future;
    }
}
