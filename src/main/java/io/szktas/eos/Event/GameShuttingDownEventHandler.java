package io.szktas.eos.Event;

import io.szktas.eos.EOSBinder.EOSNative;
import net.minecraft.Util;
import net.minecraftforge.event.GameShuttingDownEvent;
import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.common.Mod;

@Mod.EventBusSubscriber
public class GameShuttingDownEventHandler {
    @SubscribeEvent
    public static void onShutdown(GameShuttingDownEvent evt) {
        Util.shutdownExecutor(EOSNative.executor);
    }
}
