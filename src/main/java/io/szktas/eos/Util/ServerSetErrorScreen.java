package io.szktas.eos.Util;

import net.minecraft.network.chat.Component;

import static io.szktas.eos.Main.LOGGER;

public class ServerSetErrorScreen implements ISetErrorScreen{
    @Override
    public void set(Component title, Component reason, Component left, Component right) {
        LOGGER.error(reason.getString());
    }
}
