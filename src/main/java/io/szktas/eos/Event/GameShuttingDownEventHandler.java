package io.szktas.eos.Event;

import io.szktas.eos.EOSBinder.EOSNative;
import net.minecraftforge.event.GameShuttingDownEvent;
import net.minecraftforge.eventbus.api.SubscribeEvent;
import net.minecraftforge.fml.common.Mod;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.TimeUnit;

@Mod.EventBusSubscriber
public class GameShuttingDownEventHandler {
    private static void shutdownExecutor(ExecutorService pService) {
        pService.shutdown();

        boolean flag;
        try {
            flag = pService.awaitTermination(3L, TimeUnit.SECONDS);
        } catch (InterruptedException interruptedexception) {
            flag = false;
        }

        if (!flag) {
            pService.shutdownNow();
        }

    }

    @SubscribeEvent
    public static void onShutdown(GameShuttingDownEvent evt) {
        shutdownExecutor(EOSNative.executor);

    }
}
