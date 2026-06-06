package io.szktas.eos.Client.Gui;

import io.szktas.eos.Config;
import net.minecraft.client.gui.GuiGraphics;
import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.components.MultiLineTextWidget;
import net.minecraft.client.gui.components.StringWidget;
import net.minecraft.client.gui.layouts.FrameLayout;
import net.minecraft.client.gui.layouts.GridLayout;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.CommonComponents;
import net.minecraft.network.chat.Component;

import java.util.function.Function;

public class HintGui extends Screen {
    private final Runnable parent;
    private final Component reason;
    private final Component buttonText;
    private final Component buttonText2;
    private final GridLayout layout = new GridLayout();

    public static Function<Runnable, Screen> build(Component title, Component reason, Component buttonLeft, Component buttonRight) {
        return (r) -> new HintGui(r, title, reason, buttonLeft, buttonRight);
    }

    public HintGui(Runnable after, Component pTitle, Component pReason, Component pButtonText, Component button2) {
        super(pTitle);
        this.parent = after;
        this.reason = pReason;
        this.buttonText = pButtonText;
        this.buttonText2 = button2;
    }

    protected void init() {
        this.layout.defaultCellSetting().alignHorizontallyCenter().padding(10);
        GridLayout.RowHelper helper = this.layout.createRowHelper(1);
        helper.addChild(new StringWidget(this.title, this.font));
        helper.addChild((new MultiLineTextWidget(this.reason, this.font)).setMaxWidth(this.width - 50).setCentered(true));
        Button button;
        Button button2;
        button = Button.builder(this.buttonText, (p_280799_) -> {
            Config.SHOW_HINT_ON_LAUNCH.set(false);
            Config.SHOW_HINT_ON_LAUNCH.save();
            parent.run();
        }).build();
        button2 = Button.builder(this.buttonText2, (p_280799_) -> {
            parent.run();
        }).build();

        helper.addChild(button);
        helper.addChild(button2);
        this.layout.arrangeElements();
        this.layout.visitWidgets(this::addRenderableWidget);
        this.repositionElements();
    }

    protected void repositionElements() {
        FrameLayout.centerInRectangle(this.layout, this.getRectangle());
    }

    @Override
    public Component getNarrationMessage() {
        return CommonComponents.joinForNarration(this.title, this.reason);
    }

    public boolean shouldCloseOnEsc() {
        return false;
    }

    public void render(GuiGraphics pGuiGraphics, int pMouseX, int pMouseY, float pPartialTick) {
        this.renderBackground(pGuiGraphics);
        super.render(pGuiGraphics, pMouseX, pMouseY, pPartialTick);
    }
}
