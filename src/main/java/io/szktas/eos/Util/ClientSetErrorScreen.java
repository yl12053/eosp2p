package io.szktas.eos.Util;

import io.szktas.eos.Client.Gui.HintGui;
import net.minecraft.network.chat.Component;

public class ClientSetErrorScreen implements ISetErrorScreen{
    @Override
    public void set(Component title, Component reason, Component left, Component right) {
        HintGui.errorToShow = HintGui.build(title, reason, left, right);
    }
}
