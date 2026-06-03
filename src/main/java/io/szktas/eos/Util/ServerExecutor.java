package io.szktas.eos.Util;

import net.minecraftforge.server.ServerLifecycleHooks;

import java.util.concurrent.ConcurrentLinkedDeque;
import java.util.function.Consumer;

public class ServerExecutor implements Consumer<Runnable> {
    public static ConcurrentLinkedDeque<Runnable> taskQueue = new ConcurrentLinkedDeque<>();
    @Override
    public void accept(Runnable runnable) {
        var server = ServerLifecycleHooks.getCurrentServer();
        if (server != null && server.isRunning()) {
            Runnable task;
            while ((task = taskQueue.poll()) != null) {
                server.execute(task);
            }
            server.execute(runnable);
        } else {
            taskQueue.add(runnable);
        }
    }
}
