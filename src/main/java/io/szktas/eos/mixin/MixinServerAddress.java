package io.szktas.eos.mixin;

import io.szktas.eos.Client.IServerAddress;
import io.szktas.eos.EOSBinder.EOSNative;
import net.minecraft.client.multiplayer.resolver.ServerAddress;
import org.jetbrains.annotations.Nullable;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(ServerAddress.class)
public class MixinServerAddress implements IServerAddress {
    @Inject(method = "isValidAddress", at = @At("HEAD"), cancellable = true)
    private static void injectValidCheck(String pHostAndPort, CallbackInfoReturnable<Boolean> cir) {
        if (!EOSNative.isCanUse()) return;
        if (pHostAndPort.toLowerCase().startsWith("eos:")) {
            cir.setReturnValue(EOSNative.decodeConnectionKey(pHostAndPort.substring(4)) != null);
            cir.cancel();
        }
    }

    @Shadow
    @Final
    private static ServerAddress INVALID;

    @Inject(method = "parseString", at = @At("HEAD"), cancellable = true)
    private static void injectParse(String pIp, CallbackInfoReturnable<ServerAddress> cir) {
        if (!EOSNative.isCanUse()) return;
        if (pIp == null) return;
        if (pIp.toLowerCase().startsWith("eos:")) {
            String rbuf = pIp.substring(4);
            String[] buff = EOSNative.decodeConnectionKey(rbuf);
            if (buff == null) {
                cir.setReturnValue(INVALID);
                cir.cancel();
            }
            ServerAddress dummy = new ServerAddress("server.eos", 0);
            IServerAddress inj = (IServerAddress) (Object) dummy;
            inj.eosp2p$setIsEOS();
            inj.eosp2p$setPuid(buff[0]);
            inj.eosp2p$setSocket(buff[1]);
            cir.setReturnValue(dummy);
            cir.cancel();
        }
    }

    @Unique private boolean eosp2p$override = false;
    @Unique private String eosp2p$puid;
    @Unique private String eosp2p$socket;

    @Override
    public void eosp2p$setIsEOS() {
        this.eosp2p$override = true;
    }

    @Override
    public boolean eosp2p$isEOS() {
        return this.eosp2p$override;
    }

    @Override
    public @Nullable String eosp2p$getPuid() {
        return this.eosp2p$puid;
    }

    @Override
    public @Nullable String eosp2p$getSocket() {
        return this.eosp2p$socket;
    }

    @Override
    public void eosp2p$setPuid(String puid) {
        this.eosp2p$puid = puid;
    }

    @Override
    public void eosp2p$setSocket(String socket) {
        this.eosp2p$socket = socket;
    }
}
