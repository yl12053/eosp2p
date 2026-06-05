package io.szktas.eos.mixin;

import io.szktas.eos.EOSBinder.EOSNative;
import net.minecraft.network.chat.Component;
import net.minecraft.network.chat.ComponentUtils;
import net.minecraft.network.chat.MutableComponent;
import net.minecraft.server.commands.PublishCommand;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

import java.util.Objects;

import static io.szktas.eos.EOSBinder.EOSNative.PUID;

@Mixin(PublishCommand.class)
public class PublishCommandMixin {
    @Inject(method = "getSuccessMessage", at = @At("RETURN"), cancellable = true)
    private static void changeSuccessMessage(int pPort, CallbackInfoReturnable<MutableComponent> cir) {
        if (!(EOSNative.isCanUse() && PUID != null)) return;
        MutableComponent originalComp = cir.getReturnValue();

        cir.setReturnValue(originalComp.append("\n").append(Component.translatableWithFallback(
                "chat.eosp2p.show_connect",
                "Address through EOS: %s",
                ComponentUtils.copyOnClickText("EOS:" + Objects.requireNonNull(EOSNative.getConnectionKey()))
        )));
        cir.cancel();
    }
}
