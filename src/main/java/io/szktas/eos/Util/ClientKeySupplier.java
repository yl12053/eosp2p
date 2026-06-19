package io.szktas.eos.Util;

import net.minecraft.client.Minecraft;

import java.util.UUID;

import static io.szktas.eos.Config.RANDOM_CONNECT_STRING;

public class ClientKeySupplier implements IKeySupplier {
    @Override public String getBaseKey() {
        return Minecraft.getInstance().getUser().getUuid() + (RANDOM_CONNECT_STRING.get() ? UUID.randomUUID().toString() : "");
    }
}
