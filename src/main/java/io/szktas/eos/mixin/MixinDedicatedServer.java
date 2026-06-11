package io.szktas.eos.mixin;

import io.szktas.eos.EOSBinder.EOSNative;
import net.minecraft.server.dedicated.DedicatedServer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(DedicatedServer.class)
public class MixinDedicatedServer {
    @Inject(method = "stopServer", at = @At("RETURN"))
    public void injectStopServer(CallbackInfo ci) {
        if (EOSNative.isCanUse()) EOSNative.teardown();
    }
}
