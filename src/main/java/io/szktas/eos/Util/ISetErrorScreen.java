package io.szktas.eos.Util;

import net.minecraft.network.chat.Component;

public interface ISetErrorScreen {
    void set(Component title, Component reason, Component left, Component right);
}
