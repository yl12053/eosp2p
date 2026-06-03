package io.szktas.eos.Util;

import net.minecraft.client.Minecraft;

import java.util.UUID;

public class ClientKeySupplier implements IKeySupplier {
    @Override public String getBaseKey() {
        return Minecraft.getInstance().getUser().getUuid() + UUID.randomUUID();
    }
}
