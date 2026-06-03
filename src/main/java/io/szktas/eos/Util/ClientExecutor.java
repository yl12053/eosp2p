package io.szktas.eos.Util;

import net.minecraft.client.Minecraft;

import java.util.function.Consumer;

public class ClientExecutor implements Consumer<Runnable> {
    @Override
    public void accept(Runnable runnable) {
        Minecraft.getInstance().execute(runnable);
    }
}
