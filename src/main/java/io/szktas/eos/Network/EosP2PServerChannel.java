package io.szktas.eos.Network;

import io.netty.channel.*;
import io.szktas.eos.Event.ConnectIncoming;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.eventbus.api.SubscribeEvent;

import java.net.SocketAddress;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

import static io.szktas.eos.Main.LOGGER;

public class EosP2PServerChannel extends AbstractServerChannel {
    private final AtomicBoolean isClosed = new AtomicBoolean(false);
    private volatile boolean isRegistered = false;
    private final Map<ConnectionKey, EosP2PChannel> children = new ConcurrentHashMap<>();

    private EOSAddress addr = null;

    public EosP2PServerChannel() {
        super();
    }

    @Override
    public ChannelConfig config() {
        return new DefaultChannelConfig(this);
    }

    @Override
    public boolean isOpen() {
        return !isClosed.get();
    }

    @Override
    public boolean isActive() {
        return !isClosed.get() && isRegistered && (addr != null);
    }

    @Override
    protected boolean isCompatible(EventLoop loop) {
        return true;
    }

    @Override
    protected SocketAddress localAddress0() {
        return this.addr;
    }

    @Override
    protected void doRegister() throws Exception {
        super.doRegister();
        MinecraftForge.EVENT_BUS.register(this);
        isRegistered = true;
    }

    @Override
    protected void doBind(SocketAddress localAddress) throws Exception {
        if (addr != null) throw new IllegalStateException("Already bind");
        if (localAddress instanceof EOSAddress address) {
            this.addr = address;
            return;
        }
        throw new IllegalArgumentException("Not a valid EOS address pair");
    }

    @Override
    protected void doClose() throws Exception {
        LOGGER.debug("Closing Server");
        if (isClosed.compareAndSet(false, true)) {
            MinecraftForge.EVENT_BUS.unregister(this);
            children.values().forEach(EosP2PChannel::close);
            children.clear();
        }
    }

    @Override
    protected void doBeginRead() throws Exception {

    }

    @SubscribeEvent
    public void onIncomingConnection(ConnectIncoming incoming) {
        if (!Objects.equals(incoming.SelfPUID, this.addr.getPUID())) return;
        if (!Objects.equals(incoming.SocketName, this.addr.getSocketID())) return;

        eventLoop().execute(() -> {
            EOSAddress remote = new EOSAddress(incoming.RemotePUID, this.addr.getSocketID(), (byte)0);
            ConnectionKey key = new ConnectionKey(incoming.RemotePUID, incoming.SocketName, (byte) 0);
            EosP2PChannel channel = children.computeIfAbsent(key, (k) -> {
                EosP2PChannel child = new EosP2PChannel(this, addr, remote);
                // eventLoop().register(child);

                child.closeFuture().addListener(future -> {
                    this.children.remove(k);
                });

                return child;
            });

            pipeline().fireChannelRead(channel);

            ChannelPromise promise = channel.newPromise();
            channel.unsafe().connect(remote, addr, promise);

            promise.addListener(future -> {
                if (!future.isSuccess()) {
                    eventLoop().execute(() -> children.remove(key));
                    try {
                        channel.close();
                    } catch (IllegalStateException ignored) {

                    }
                    LOGGER.error("Failed to accept connection", future.cause());
                }
            });
        });

    }
}
