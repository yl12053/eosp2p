package io.szktas.eos.EOSBinder;

import io.szktas.eos.Client.Gui.HintGui;
import io.szktas.eos.Config;
import io.szktas.eos.Main;
import io.szktas.eos.Util.*;
import lombok.extern.slf4j.Slf4j;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;
import net.minecraftforge.fml.DistExecutor;
import net.minecraftforge.fml.ModList;
import org.apache.commons.lang3.SystemUtils;
import org.apache.logging.log4j.util.TriConsumer;
import org.slf4j.Marker;
import org.slf4j.MarkerFactory;

import javax.annotation.Nullable;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;
import java.util.Base64;
import java.util.HexFormat;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Supplier;

import static io.szktas.eos.Main.LOGGER;
import static java.lang.System.getProperty;

@Slf4j
public class EOSNative {
    public static boolean isCanUse() {
        return EOSNative.IsEnabled && EOSNative.IsRunningEOS;
    }

    public static String SOCKET_NAME = null;

    public static String getConnectionKey() {
        if (PUID == null) return null;
        byte[] pb = HexFormat.of().parseHex(PUID);
        byte[] sn = HexFormat.of().parseHex(SOCKET_NAME);
        byte[] newByteArray = new byte[pb.length + sn.length + 1];
        newByteArray[0] = (byte) pb.length;
        System.arraycopy(pb, 0, newByteArray, 1, pb.length);
        System.arraycopy(sn, 0, newByteArray, pb.length + 1, sn.length);

        return Base64.getEncoder().withoutPadding().encodeToString(newByteArray);
    }

    public static String[] decodeConnectionKey(String s) {
        byte[] rawByteArray;
        try {
            rawByteArray = Base64.getDecoder().decode(s);
        } catch (IllegalArgumentException ignored) {
            return null;
        }
        if (rawByteArray.length == 0) return null;
        int lengthPB = Byte.toUnsignedInt(rawByteArray[0]);
        if (rawByteArray.length < lengthPB + 1) return null;
        if (lengthPB == 0) return null;

        byte[] pb = Arrays.copyOfRange(rawByteArray, 1, lengthPB + 1);
        byte[] sn = Arrays.copyOfRange(rawByteArray, lengthPB + 1, rawByteArray.length);

        String remotePuid = HexFormat.of().formatHex(pb).toLowerCase();
        String socketId = HexFormat.of().formatHex(sn).toLowerCase();

        return new String[]{ remotePuid, socketId };
    }

    public enum Reason {
        LIBRARY_NOT_FOUND, LIBRARY_CORRUPTED, UNSUPPORTED_ARCH, UNSUPPORTED_OS;
    }

    public enum ReasonEOS {
        PUID_INIT_FAIL, SERVICE_INIT_FAIL, PLATFORM_INIT_FAIL, CONNECTION_INIT_FAIL, P2P_INIT_FAIL,
        SUBSCRIBE_INCOMING_CONNECTION_FAILED, SUBSCRIBE_ESTABLISHED_CONNECTION_FAILED, SUBSCRIBE_INTERRUPT_CONNECTION_FAILED, SUBSCRIBE_CLOSE_CONNECTION_FAILED;
    }
    private static final String prefix = UUID.randomUUID() + Main.MODID;
    public static boolean IsEnabled;
    public static boolean IsRunningEOS = false;
    public static String reasonServiceInitFail = null;

    public static final Reason reason;
    public static ReasonEOS reasonEOS;

    private static class UnsupportedArch extends Throwable {}

    private static class UnsupportedOS extends Throwable {}

    private static final Marker EOSMarker = MarkerFactory.getMarker("EOS Native");

    public final static INameSupplier NAME_SUPPLIER = DistExecutor.safeRunForDist(
            () -> ClientNameSupplier::new,
            () -> ServerNameSupplier::new
    );

    public final static Consumer<Runnable> EXECUTOR = DistExecutor.safeRunForDist(
            () -> ClientExecutor::new,
            () -> ServerExecutor::new
    );

    public final static ISetErrorScreen SET_ERROR_SCREEN = DistExecutor.safeRunForDist(
            () -> ClientSetErrorScreen::new,
            () -> ServerSetErrorScreen::new
    );

    static {
        boolean isEnabledProcess = false;
        Reason reasonProcess = null;
        try {
            if (SystemUtils.IS_OS_MAC) {
                System.load(loadLibraryFromClass("/libEOSSDK-Mac-Shipping.dylib", Main.MODID + "_eos.dylib"));
                System.load(loadLibraryFromClass("/META-INF/binder/mac.dylib"));
            } else {
                String arch = SystemUtils.OS_ARCH.toLowerCase();
                boolean isArm64 = false;
                if (arch.contains("aarch64") || arch.contains("arm64")) {
                    isArm64 = true;
                } else if (!(arch.contains("amd64") || arch.contains("x86_64"))) {
                    SET_ERROR_SCREEN.set(
                            Component.translatable("command.eosp2p.unsupported_arch"),
                            Component.translatable("gui.eosp2p.pretty_os_arch", SystemUtils.OS_ARCH),
                            Component.translatable("gui.eosp2p.dismiss_ever"),
                            Component.translatable("gui.eosp2p.dismiss")
                    );
                    throw new UnsupportedArch();
                }
                LOGGER.debug("Vendor: {}", getProperty("java.vendor"));
                if (getProperty("java.vendor").contains("Android")) {
                    System.load(loadLibraryFromClass("/EOSSDK-A" + (isArm64 ? "ARM64" : "X64") + ".so", Main.MODID + "_eos.so"));
                    System.load(loadLibraryFromClass("/META-INF/binder/android" + (isArm64 ? "arm64" : "x64") + ".so"));
                } else if (SystemUtils.IS_OS_WINDOWS) {
                    // System.load(loadLibraryFromClass("/" + (isArm64 ? "arm64" : "x64") + "/xaudio2_9redist.dll"));
                    System.load(loadLibraryFromClass("/EOSSDK-Win64-Shipping" + (isArm64 ? "arm64" : "") + ".dll", Main.MODID + "_eos.dll"));
                    System.load(loadLibraryFromClass("/META-INF/binder/win" + (isArm64 ? "arm64" : "x64") + ".dll"));
                } else if (SystemUtils.IS_OS_LINUX) {
                    System.load(loadLibraryFromClass("/libEOSSDK-Linux" + (isArm64 ? "Arm64" : "") + "-Shipping.so", Main.MODID + "_eos.so"));
                    System.load(loadLibraryFromClass("/META-INF/binder/linux" + (isArm64 ? "arm64" : "x64") + ".so"));
                } else {
                    SET_ERROR_SCREEN.set(
                            Component.translatable("command.eosp2p.unsupported_os"),
                            Component.translatable("gui.eosp2p.pretty_os_hint", SystemUtils.OS_NAME),
                            Component.translatable("gui.eosp2p.dismiss_ever"),
                            Component.translatable("gui.eosp2p.dismiss")
                    );
                    throw new UnsupportedOS();
                }
            }
            LOGGER.info("Init ongoing");
            init(
                    () -> {
                        IsEnabled = true;
                        Runtime.getRuntime().addShutdownHook(new Thread(EOSNative::teardown, "EOS-Teardown"));
                        LOGGER.info("Enable, setting log");
                        SetLogging((level, s) -> {
                            if (level < 300) {
                                LOGGER.error(EOSMarker, s);
                            } else if (level < 400) {
                                LOGGER.warn(EOSMarker, s);
                            } else if (level < 500) {
                                LOGGER.info(EOSMarker, s);
                            } else {
                                LOGGER.debug(EOSMarker, s);
                            }
                        });
                    },
                    (reason) -> {
                        LOGGER.error("Error in EOS Service Initialization: {}", reason);
                    }
            );
        } catch (IOException e) {
            LOGGER.error("Error Loading EOS Service", e);
            SET_ERROR_SCREEN.set(
                    Component.translatable("command.eosp2p.library_not_found"),
                    Component.translatable("gui.eosp2p.library_not_found"),
                    Component.translatable("gui.eosp2p.dismiss_ever"),
                    Component.translatable("gui.eosp2p.dismiss")
            );
            reasonProcess = Reason.LIBRARY_NOT_FOUND;
        } catch (UnsatisfiedLinkError e) {
            LOGGER.error("Failed to load library", e);
            SET_ERROR_SCREEN.set(
                    Component.translatable("command.eosp2p.library_corrupted"),
                    Component.translatable("gui.eosp2p.library_not_found"),
                    Component.translatable("gui.eosp2p.dismiss_ever"),
                    Component.translatable("gui.eosp2p.dismiss")
            );
            reasonProcess = Reason.LIBRARY_CORRUPTED;
        } catch (UnsupportedArch e) {
            LOGGER.error("Unsupported Architecture: {}", SystemUtils.OS_ARCH);
            reasonProcess = Reason.UNSUPPORTED_ARCH;
        } catch (UnsupportedOS e) {
            LOGGER.error("Unsupported Operation System: {}", SystemUtils.OS_NAME);
            reasonProcess = Reason.UNSUPPORTED_OS;
        } finally {
            IsEnabled = IsEnabled || isEnabledProcess;
            reason = reasonProcess;
        }
    }

    private static String loadLibraryFromClass(String path) throws IOException {
        return loadLibraryFromClass(path, null);
    }

    private static String loadLibraryFromClass(String path, @Nullable String outname) throws IOException {
        String[] splitPath = path.split("/");
        String fn = Main.MODID + "_" + splitPath[splitPath.length - 1];
        splitPath[splitPath.length - 1] = fn;

        File parentDir = new File(getProperty("java.io.tmpdir"), prefix);
        if (!parentDir.exists()) {
            if (!parentDir.mkdirs()) {
                throw new IOException("Cannot create folder");
            };
            parentDir.deleteOnExit();
        }
        File tempFile = new File(parentDir, outname == null ? fn : outname);

        tempFile.deleteOnExit();

        LOGGER.info("Trying to obtain: {}", "/" + String.join("/", splitPath));
        try (InputStream is = EOSNative.class.getResourceAsStream("/" + String.join("/", splitPath))) {
            if (is == null) throw new IOException("Library not found: " + path);
            Files.copy(is, tempFile.toPath(), StandardCopyOption.REPLACE_EXISTING);
        }

        return tempFile.getAbsolutePath();
    }

    public static native void SetLogging(BiConsumer<Integer, String> logging);

    private static final AtomicBoolean isPUIDInit = new AtomicBoolean(false);
    public static String PUID = null;

    public static void getPUID(Consumer<String> resolve, Runnable reject) {
        if (isPUIDInit.compareAndSet(false, true)) {
            IKeySupplier provider = DistExecutor.safeRunForDist(
                    () -> ClientKeySupplier::new,
                    () -> ServerKeySupplier::new
            );
            getPUID(provider.getBaseKey() + Main.MODID, (p) -> {
                if (p == null) {
                    IsRunningEOS = false;
                    reasonEOS = ReasonEOS.PUID_INIT_FAIL;
                    reject.run();
                    return;
                }
                PUID = p;
                IsRunningEOS = true;
                resolve.accept(p);
            });
        }
    }

    public static native void getPUID(String key, Consumer<String> callback);

    public static native void SetNameGetter(Supplier<String> supplier);

    private static native void init(String name, String version, Consumer<String> ret);
    private static native void initConnectionHandle(double timeout, String pid, String credid, String secret, String sandboxid, String depid, Consumer<Integer> consumer);
    public static void initConnectionHandle(Runnable resolve, Consumer<Integer> reject) {
        LOGGER.debug("Trying to start init connection handle");
        initConnectionHandle(
                Config.TIMEOUT.get(),
                Config.PRODUCT_ID.get(),
                Config.CLIENT_CREDENTIAL.get(),
                Config.CLIENT_SECRET.get(),
                Config.SANDBOX_ID.get(),
                Config.DEPLOY_ID.get(),
                (ret) -> {
                    LOGGER.debug("Got reply with ans: {}", ret);
                    switch (ret) {
                        case 0:
                            IsRunningEOS = true;
                            break;
                        case 1:
                            IsRunningEOS = false;
                            reasonEOS = ReasonEOS.PLATFORM_INIT_FAIL;
                            break;
                        case 2:
                            IsRunningEOS = false;
                            reasonEOS = ReasonEOS.CONNECTION_INIT_FAIL;
                            break;
                        case 3:
                            IsRunningEOS = false;
                            reasonEOS = ReasonEOS.P2P_INIT_FAIL;
                            break;
                        case -1:
                            IsRunningEOS = false;
                            LOGGER.error("Tries to init connection when error occurs in prior stage");
                            break;
                        default:
                            LOGGER.error("Unknown status code {}", ret);
                    }
                    if (ret == 0) {
                        resolve.run();
                    } else {
                        SET_ERROR_SCREEN.set(
                                Component.translatable("command.eosp2p." + reasonEOS.name().toLowerCase()),
                                Component.translatable("gui.eosp2p.init_failed"),
                                Component.translatable("gui.eosp2p.dismiss_ever"),
                                Component.translatable("gui.eosp2p.dismiss")
                        );
                        reject.accept(ret);
                    }
                }
        );
    }

    private static void init(Runnable resolve, Consumer<String> reject) {
        String version = ModList.get().getModContainerById(Main.MODID)
                .map(container -> container.getModInfo().getVersion().toString())
                .orElse("Unknown");
        init(Main.MODID, version, (ret) -> {
            if (ret != null) {
                IsRunningEOS = false;
                reasonServiceInitFail = ret;
                reasonEOS = ReasonEOS.SERVICE_INIT_FAIL;
                reject.accept(ret);
            }
            resolve.run();
        });
    }

    public static native void setNetworkStatus(int status);

    public static final ScheduledExecutorService executor = Executors.newSingleThreadScheduledExecutor();

    public static native void subscribeIncomingConnectionRequestHandler(TriConsumer<String, String, String> consumer);
    public static native boolean subscribeIncomingConnectionRequest(String puid, String socketname);

    public static native void subscribeEstablishedConnectionRequestHandler(TriConsumer<String, String, String> consumer);
    public static native boolean subscribeEstablishedConnectionRequest(String puid, String socketname);

    public static native void subscribeInterruptConnectionRequestHandler(TriConsumer<String, String, String> consumer);
    public static native boolean subscribeInterruptConnectionRequest(String puid, String socketname);

    public static native void subscribeCloseConnectionRequestHandler(QuadConsumer<String, String, String, String> consumer);
    public static native boolean subscribeCloseConnectionRequest(String puid, String socketname);

    public static native String connectOrAccept(String localPUID, String remotePUID, String SocketID);

    public static native String close(String localPUID, String remotePUID, String SocketID);

    public static native String send(String localPUID, String remotePUID, String socketId, byte channel, byte[] bytes);

    public static native void registerReceiveCallbackFor(String localPUID, PacketConsumer resultConsumer);

    public static native void teardown();
}
