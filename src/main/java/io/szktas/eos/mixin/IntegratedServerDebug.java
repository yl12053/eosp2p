package io.szktas.eos.mixin;

import com.mojang.datafixers.DataFixer;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.client.server.IntegratedServer;
import net.minecraft.client.server.LanServerPinger;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.Services;
import net.minecraft.server.WorldStem;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.level.progress.ChunkProgressListenerFactory;
import net.minecraft.server.packs.repository.PackRepository;
import net.minecraft.world.level.GameType;
import net.minecraft.world.level.storage.LevelStorageSource;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.Redirect;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import javax.annotation.Nullable;
import java.io.IOException;
import java.net.InetAddress;
import java.net.Proxy;

import static io.szktas.eos.Main.LOGGER;

@Mixin(IntegratedServer.class)
public abstract class IntegratedServerDebug extends MinecraftServer {
    @Shadow
    @Final
    private Minecraft minecraft;

    @Shadow
    private int publishedPort;

    @Shadow
    @Nullable
    private LanServerPinger lanPinger;

    @Shadow
    @Nullable
    private GameType publishedGameType;

    public IntegratedServerDebug(Thread pServerThread, LevelStorageSource.LevelStorageAccess pStorageSource, PackRepository pPackRepository, WorldStem pWorldStem, Proxy pProxy, DataFixer pFixerUpper, Services pServices, ChunkProgressListenerFactory pProgressListenerFactory) {
        super(pServerThread, pStorageSource, pPackRepository, pWorldStem, pProxy, pFixerUpper, pServices, pProgressListenerFactory);
    }

    @Inject(method = "publishServer", at = @At(value = "HEAD"), cancellable = true)
    public void redirServer(GameType pGameMode, boolean pCheats, int pPort, CallbackInfoReturnable<Boolean> cir) {

        try {
            this.minecraft.prepareForMultiplayer();
            this.minecraft.getProfileKeyPairManager().prepareKeyPair().thenAcceptAsync((p_263550_) -> {
                p_263550_.ifPresent((p_263549_) -> {
                    ClientPacketListener clientpacketlistener = this.minecraft.getConnection();
                    if (clientpacketlistener != null) {
                        clientpacketlistener.setKeyPair(p_263549_);
                    }

                });
            }, this.minecraft);
            this.getConnection().startTcpServerListener((InetAddress)null, pPort);
            LOGGER.info("Started serving on {}", (int)pPort);
            this.publishedPort = pPort;
            this.lanPinger = new LanServerPinger(this.getMotd(), "" + pPort);
            this.lanPinger.start();
            this.publishedGameType = pGameMode;
            this.getPlayerList().setAllowCheatsForAllPlayers(pCheats);
            int i = this.getProfilePermissions(this.minecraft.player.getGameProfile());
            this.minecraft.player.setPermissionLevel(i);

            for(ServerPlayer serverplayer : this.getPlayerList().getPlayers()) {
                this.getCommands().sendCommands(serverplayer);
            }

            cir.setReturnValue(true);
            cir.cancel();
        } catch (IOException ioexception) {
            LOGGER.error("Error: ", ioexception);
            cir.setReturnValue(false);
            cir.cancel();
        }
    }
}
