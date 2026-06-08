package io.szktas.eos.Event;

import lombok.AllArgsConstructor;
import lombok.Getter;
import lombok.Setter;
import net.minecraftforge.eventbus.api.Event;

import javax.annotation.Nullable;

import static io.szktas.eos.EOSBinder.EOSNative.connectOrAccept;

@Getter
@Setter
@AllArgsConstructor
public class ConnectIncoming extends Event {
    public final String SelfPUID;
    public final String RemotePUID;
    @Nullable public final String SocketName;

    public String accept() {
        return connectOrAccept(SelfPUID, RemotePUID, SocketName);
    }
}
