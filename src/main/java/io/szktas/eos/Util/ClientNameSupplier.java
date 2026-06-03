package io.szktas.eos.Util;

import net.minecraft.client.Minecraft;

public class ClientNameSupplier implements INameSupplier {
    @Override public String getName() {
        return Minecraft.getInstance().getUser().getName();
    }
}
