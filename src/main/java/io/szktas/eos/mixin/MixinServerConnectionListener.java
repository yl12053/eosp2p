package io.szktas.eos.mixin;

import com.llamalad7.mixinextras.sugar.Local;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.*;
import io.netty.channel.epoll.Epoll;
import io.netty.handler.timeout.ReadTimeoutHandler;
import io.szktas.eos.EOSBinder.EOSNative;
import io.szktas.eos.Network.EOSAddress;
import io.szktas.eos.Network.EosP2PServerChannel;
import net.minecraft.network.Connection;
import net.minecraft.network.RateKickingConnection;
import net.minecraft.network.protocol.PacketFlow;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.network.LegacyQueryHandler;
import net.minecraft.server.network.ServerConnectionListener;
import net.minecraft.server.network.ServerHandshakePacketListenerImpl;
import net.minecraft.util.LazyLoadedValue;
import org.slf4j.Logger;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

import java.net.InetAddress;
import java.util.List;

import static io.szktas.eos.EOSBinder.EOSNative.PUID;
import static io.szktas.eos.EOSBinder.EOSNative.SOCKET_NAME;

@Mixin(ServerConnectionListener.class)
public class MixinServerConnectionListener {
    @Shadow
    @Final
    private List<ChannelFuture> channels;

    @Shadow(remap = false)
    @Final
    private static int READ_TIMEOUT;

    @Shadow
    @Final
    MinecraftServer server;

    @Shadow
    @Final
    List<Connection> connections;

    @Shadow
    @Final
    private static Logger LOGGER;

    @Inject(
            method = "startTcpServerListener",
            at = @At(
                    value = "INVOKE",
                    target = "Ljava/util/List;add(Ljava/lang/Object;)Z",
                    ordinal = 0,
                    shift = At.Shift.AFTER
            )
    )
    public void injectAfterStartTCP(InetAddress pAddress, int pPort, CallbackInfo ci, @Local LazyLoadedValue<? extends EventLoopGroup> lazyloadedvalue) {
        if (!EOSNative.isCanUse()) return;
        ServerConnectionListener zThis = (ServerConnectionListener) (Object) this;
        this.channels.add(
                new ServerBootstrap()
                        .channel(EosP2PServerChannel.class)
                        .childHandler(new ChannelInitializer<>() {
                            @Override
                            protected void initChannel(Channel ch) throws Exception {
                                ChannelPipeline channelpipeline = ch.pipeline().addLast("timeout", new ReadTimeoutHandler(READ_TIMEOUT)).addLast("legacy_query", new LegacyQueryHandler(zThis));
                                Connection.configureSerialization(channelpipeline, PacketFlow.SERVERBOUND);
                                int i = server.getRateLimitPacketsPerSecond();
                                Connection connection = i > 0 ? new RateKickingConnection(i) : new Connection(PacketFlow.SERVERBOUND);
                                connections.add(connection);
                                channelpipeline.addLast("packet_handler", connection);
                                connection.setListener(new ServerHandshakePacketListenerImpl(server, connection));
                            }
                        })
                        .group(lazyloadedvalue.get())
                        .localAddress(new EOSAddress(PUID, SOCKET_NAME, (byte) 0))
                        .bind()
                        .syncUninterruptibly()
        );
        LOGGER.info("Started serving on EOS:{}:{}", PUID, SOCKET_NAME);
        LOGGER.info("Connection address: [EOS:{}]", EOSNative.getConnectionKey());
    }
}
