package io.szktas.eos.mixin;

import io.szktas.eos.Client.EOSResolveAddress;
import io.szktas.eos.Client.IServerAddress;
import io.szktas.eos.EOSBinder.EOSNative;
import net.minecraft.client.multiplayer.resolver.ResolvedServerAddress;
import net.minecraft.client.multiplayer.resolver.ServerAddress;
import net.minecraft.client.multiplayer.resolver.ServerNameResolver;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import java.util.Optional;

@Mixin(ServerNameResolver.class)
public class MixinServerNameResolver {
    @Inject(method = "resolveAddress", at = @At("HEAD"), cancellable = true)
    public void injectResolveAddress(ServerAddress pServerAddress, CallbackInfoReturnable<Optional<ResolvedServerAddress>> cir) {
        if (!EOSNative.isCanUse()) return;
        if (pServerAddress == null) return;
        IServerAddress addr = (IServerAddress) (Object) pServerAddress;
        if (addr.eosp2p$isEOS()) {
            ResolvedServerAddress ret = new EOSResolveAddress(addr.eosp2p$getPuid(), addr.eosp2p$getSocket(), (byte) 0);
            cir.setReturnValue(Optional.of(ret));
            cir.cancel();
        }
    }
}
