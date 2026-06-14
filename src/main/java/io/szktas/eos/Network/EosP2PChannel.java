package io.szktas.eos.Network;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.channel.*;
import io.netty.util.ReferenceCountUtil;
import io.szktas.eos.EOSBinder.EOSNative;
import io.szktas.eos.Event.ConnectClose;
import io.szktas.eos.Event.ConnectEstablished;
import io.szktas.eos.Event.ConnectInterrupt;
import lombok.extern.slf4j.Slf4j;
import net.minecraft.network.chat.Component;
import net.minecraftforge.common.MinecraftForge;
import net.minecraftforge.common.util.LazyOptional;
import net.minecraftforge.eventbus.api.SubscribeEvent;

import javax.annotation.Nullable;
import java.io.IOException;
import java.net.SocketAddress;
import java.nio.channels.ConnectionPendingException;
import java.util.ArrayDeque;
import java.util.Objects;
import java.util.Queue;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

import static io.szktas.eos.EOSBinder.EOSNative.*;
import static io.szktas.eos.Main.LOGGER;

@Slf4j
public class EosP2PChannel extends AbstractChannel {
    public final EOSAddress local;
    public final EOSAddress remote;

    private final ChannelMetadata metadata = new ChannelMetadata(false);
    private final LazyOptional<ChannelConfig> config = LazyOptional.of(() -> new DefaultChannelConfig(this));
    private volatile boolean active = false;

    private volatile boolean readPending = false;

    private AtomicBoolean isClosed = new AtomicBoolean(false);
    private final Queue<ByteBuf> queue = new ArrayDeque<>();

    public EosP2PChannel(Channel parent, EOSAddress local, EOSAddress remote) {
        super(parent);
        this.local = local;
        this.remote = remote;
    }

    public void setConnected() {
        this.active = true;
    }

    @Override
    protected AbstractUnsafe newUnsafe() {
        return new Unsafe();
    }

    @Override
    protected boolean isCompatible(EventLoop loop) {
        return true;
    }

    @Override
    protected SocketAddress localAddress0() {
        return this.local;
    }

    @Override
    protected SocketAddress remoteAddress0() {
        return this.remote;
    }

    @Override
    protected void doBind(SocketAddress localAddress) throws Exception {
        throw new UnsupportedOperationException("This is a client channel");
    }

    @Override
    protected void doDisconnect() throws Exception {
        doClose();
    }

    @Override
    protected void doClose() throws Exception {
        if (isClosed.compareAndSet(false, true)) {
            EOSNative.close(local.getPUID(), remote.getPUID(), remote.getSocketID());
            ((Unsafe) this.unsafe()).unregister();
            queue.forEach(ReferenceCountUtil::safeRelease);
        }
    }

    @Override
    protected void doBeginRead() throws Exception {
        this.readPending = true;
        ByteBuf buffer;
        while ((buffer = queue.poll()) != null) {
            pipeline().fireChannelRead(buffer);
        }
        if (this.readPending && !config().isAutoRead()) {
            pipeline().fireChannelReadComplete();
        }
    }

    @Override
    protected void doWrite(ChannelOutboundBuffer in) throws Exception {
        while (true) {
            Object msg = in.current();
            if (msg == null) break;

            if (msg instanceof ByteBuf buf) {
                if (!buf.isReadable()) {
                    in.remove();
                    continue;
                }

                try {
                    int readableBytes = buf.readableBytes();
                    byte[] data = new byte[readableBytes];
                    buf.readBytes(data);

                    String err = send(local.getPUID(), remote.getPUID(), remote.getSocketID(), remote.getChannelID(), data);
                    if (err == null) {
                        in.remove();
                    } else {
                        LOGGER.error("Received error: {}", err);
                        throw new IOException(err);
                    }
                } catch (Throwable t) {
                    in.remove(t);
                    throw t;
                }
            } else {
                UnsupportedOperationException error = new UnsupportedOperationException("Unsupported type: " + msg.getClass().getName());
                in.remove(error);
                LOGGER.error("Unknown type: ", error);
                ReferenceCountUtil.safeRelease(msg);
            }
        }
    }

    @Override
    public ChannelConfig config() {
        return this.config.orElse(null);
    }

    @Override
    public boolean isOpen() {
        return !isClosed.get();
    }

    @Override
    public boolean isActive() {
        return !isClosed.get() && this.active;
    }

    @Override
    public ChannelMetadata metadata() {
        return this.metadata;
    }

    public class Unsafe extends AbstractUnsafe {
        @Nullable private ChannelPromise connectPromise = null;
        private ScheduledFuture<?> connectTimeoutFuture = null;
        public Unsafe() {
            super();
        }

        public void onData(byte[] datas) {
            if (datas == null || datas.length == 0) return;
            eventLoop().execute(() -> {
                ByteBuf buf = Unpooled.copiedBuffer(datas);

                if (readPending) {
                    if (!config().isAutoRead()) readPending = false;
                    pipeline().fireChannelRead(buf);
                    pipeline().fireChannelReadComplete();
                } else {
                    queue.add(buf);
                }
            });
        }
        public PacketHandler dataHandler = this::onData;

        public void unregister() {
            MinecraftForge.EVENT_BUS.unregister(Unsafe.this);
            NetworkUtil.DeleteCallback(EosP2PChannel.this.remote.getPUID(), EosP2PChannel.this.remote.getSocketID(), EosP2PChannel.this.remote.getChannelID(), dataHandler);
        }

        @Override
        public void connect(SocketAddress remoteAddress, SocketAddress localAddress, ChannelPromise promise) {
            if (!promise.setUncancellable() || !ensureOpen(promise)) {
                LOGGER.debug("Failed first pass");
                return;
            }

            try {
                if (connectPromise != null) {
                    LOGGER.debug("Already pending");
                    throw new ConnectionPendingException();
                }

                MinecraftForge.EVENT_BUS.register(Unsafe.this);
                NetworkUtil.RegisterCallback(EosP2PChannel.this.remote.getPUID(), EosP2PChannel.this.remote.getSocketID(), EosP2PChannel.this.remote.getChannelID(), dataHandler);

                // connectOrAccept(PUID, EosP2PChannel.this.remote.getPUID(), EosP2PChannel.this.remote.getSocketID());

                connectPromise = promise;

                int connectTimeoutMillis = config().getConnectTimeoutMillis();
                if (connectTimeoutMillis > 0) {
                    connectTimeoutFuture = eventLoop().schedule(() -> {
                        ChannelPromise connectPromise = Unsafe.this.connectPromise;
                        ConnectTimeoutException cause =
                                new ConnectTimeoutException("Connection timed out: " + remoteAddress);
                        if (connectPromise != null && connectPromise.tryFailure(cause)) {
                            LOGGER.debug("Close Timeout");
                            close(voidPromise());
                            unregister();
                        }
                    }, connectTimeoutMillis, TimeUnit.MILLISECONDS);
                }

                promise.addListener((ChannelFutureListener) future -> {
                    if (future.isCancelled()) {
                        if (connectTimeoutFuture != null) {
                            connectTimeoutFuture.cancel(false);
                        }
                        connectPromise = null;
                        LOGGER.debug("Close cancel: ", new RuntimeException("Close stack"));
                        close(voidPromise());
                    }
                });

                String ret = connectOrAccept(PUID, EosP2PChannel.this.remote.getPUID(), EosP2PChannel.this.remote.getSocketID());
                if (ret != null) {
                    if (ret.startsWith("--")) {
                        String error = ret.substring(2);
                        eventLoop().execute(() -> {
                            if (connectTimeoutFuture != null) connectTimeoutFuture.cancel(false);
                            connectTimeoutFuture = null;
                            if (connectPromise != null && connectPromise.tryFailure(new IOException(Component.translatable("error.eosp2p." + error).getString()))) {
                                LOGGER.debug("Close error: {}", ret);
                                close(voidPromise());
                                unregister();
                            }
                        });
                    } else {
                        eventLoop().execute(() -> {
                            if (connectTimeoutFuture != null) connectTimeoutFuture.cancel(false);
                            connectTimeoutFuture = null;
                            if (connectPromise != null && connectPromise.tryFailure(new IOException(Component.translatable("error.eosp2p.generic", ret).getString()))) {
                                close(voidPromise());
                                unregister();
                            }
                        });
                    }
                }
            } catch (Throwable t) {
                promise.tryFailure(annotateConnectException(t, remoteAddress));
                closeIfClosed();
                unregister();
            }
        }

        private void fulfillConnectPromise(ChannelPromise promise, boolean wasActive) {
            if (promise == null) {
                return;
            }

            boolean active = isActive();

            boolean promiseSet = promise.trySuccess();

            if (!wasActive && active) {
                pipeline().fireChannelActive();
            }

            if (!promiseSet) {
                close(voidPromise());
            }
        }

        private void fulfillConnectPromise(ChannelPromise promise, Throwable cause) {
            if (promise == null) {
                return;
            }

            promise.tryFailure(cause);
            closeIfClosed();
        }

        @SubscribeEvent
        public void onConnectionEstablish(ConnectEstablished event) {
            if (!Objects.equals(EosP2PChannel.this.local.getPUID(), event.SelfPUID)) return;
            if (!Objects.equals(EosP2PChannel.this.remote.getPUID(), event.RemotePUID)) return;
            if (!Objects.equals(EosP2PChannel.this.remote.getSocketID(), event.SocketName)) return;

            eventLoop().execute(() -> {
                try {
                    EosP2PChannel.this.active = true;
                    fulfillConnectPromise(connectPromise, isActive());
                    pipeline().fireChannelActive();
                } catch (Throwable t) {
                    fulfillConnectPromise(connectPromise, annotateConnectException(t, null));
                    unregister();
                } finally {
                    if (connectTimeoutFuture != null) connectTimeoutFuture.cancel(false);
                    connectPromise = null;
                }
            });
        }

        @SubscribeEvent
        public void onConnectionInterrupt(ConnectInterrupt event) {
            if (!Objects.equals(EosP2PChannel.this.local.getPUID(), event.SelfPUID)) return;
            if (!Objects.equals(EosP2PChannel.this.remote.getPUID(), event.RemotePUID)) return;
            if (!Objects.equals(EosP2PChannel.this.remote.getSocketID(), event.SocketName)) return;

            eventLoop().execute(() -> {
                if (connectTimeoutFuture != null) connectTimeoutFuture.cancel(false);
                Throwable exception = new IOException("Connection interrupted by unknown reason");
                if (connectPromise != null) {
                    connectPromise.tryFailure(exception);
                }
                pipeline().fireExceptionCaught(exception);
                pipeline().fireChannelInactive();
                pipeline().fireChannelUnregistered();
                EosP2PChannel.this.close();
                close(voidPromise());
                unregister();
            });
        }

        @SubscribeEvent
        public void onConnectionClose(ConnectClose event) {
            if (!Objects.equals(EosP2PChannel.this.local.getPUID(), event.SelfPUID)) return;
            if (!Objects.equals(EosP2PChannel.this.remote.getPUID(), event.RemotePUID)) return;
            if (!Objects.equals(EosP2PChannel.this.remote.getSocketID(), event.SocketName)) return;

            eventLoop().execute(() -> {
                if (Objects.equals(event.reason, "EOS_CCR_Unknown")) {
                    if (connectPromise != null) {
                        LOGGER.debug("Retry due to unknown error on initialization phase");
                        String ret = connectOrAccept(PUID, EosP2PChannel.this.remote.getPUID(), EosP2PChannel.this.remote.getSocketID());
                        if (ret != null && ret.startsWith("--")) {
                            String error = ret.substring(2);
                            if (connectTimeoutFuture != null) connectTimeoutFuture.cancel(false);
                            connectTimeoutFuture = null;
                            Throwable exc2 = new IOException(Component.translatable("error.eosp2p." + error).getString());
                            if (connectPromise.tryFailure(exc2)) {
                                LOGGER.debug("Close error: {}", ret);
                                pipeline().fireExceptionCaught(exc2);
                                pipeline().fireChannelInactive();
                                pipeline().fireChannelUnregistered();
                                EosP2PChannel.this.close();
                                close(voidPromise());
                                unregister();
                            }
                        }
                        return;
                    }
                }
                if (connectTimeoutFuture != null) connectTimeoutFuture.cancel(false);
                Throwable exception = new IOException("Connection closed: " + event.reason);
                if (connectPromise != null) {
                    connectPromise.tryFailure(exception);
                }
                pipeline().fireExceptionCaught(exception);
                pipeline().fireChannelInactive();
                pipeline().fireChannelUnregistered();
                EosP2PChannel.this.close();
                close(voidPromise());
                unregister();
            });
        }
    }
}
