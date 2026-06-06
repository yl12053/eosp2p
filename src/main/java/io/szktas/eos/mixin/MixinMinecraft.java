package io.szktas.eos.mixin;

import com.llamalad7.mixinextras.injector.wrapmethod.WrapMethod;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.mojang.realmsclient.client.RealmsClient;
import io.szktas.eos.Config;
import io.szktas.eos.EOSBinder.EOSNative;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.client.main.GameConfig;
import net.minecraft.server.packs.resources.ReloadInstance;
import org.jetbrains.annotations.Nullable;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;

@Mixin(Minecraft.class)
public abstract class MixinMinecraft {
    @Shadow
    public abstract void setScreen(@Nullable Screen pGuiScreen);

    @WrapMethod(method = "setInitialScreen")
    public void wrapInitial(RealmsClient pRealmsClient, ReloadInstance pReloadInstance, GameConfig.QuickPlayData pQuickPlayData, Operation<Void> original) {
        if (!Config.SHOW_HINT_ON_LAUNCH.get()) {
            original.call(pRealmsClient, pReloadInstance, pQuickPlayData);
            return;
        }
        if (EOSNative.errorNeedShow == null) {
            original.call(pRealmsClient, pReloadInstance, pQuickPlayData);
            return;
        }
        this.setScreen(EOSNative.errorNeedShow.apply(() -> original.call(pRealmsClient, pReloadInstance, pQuickPlayData)));
    }
}
