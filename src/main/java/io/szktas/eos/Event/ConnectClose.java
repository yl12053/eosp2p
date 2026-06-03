package io.szktas.eos.Event;

import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;
import net.minecraftforge.eventbus.api.Event;

import javax.annotation.Nullable;

@Getter
@Setter
@AllArgsConstructor
public class ConnectClose extends Event {
    public final String SelfPUID;
    public final String RemotePUID;
    @Nullable public final String SocketName;
}
