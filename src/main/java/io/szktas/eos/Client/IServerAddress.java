package io.szktas.eos.Client;

import javax.annotation.Nullable;

public interface IServerAddress {
    boolean eosp2p$isEOS();
    void eosp2p$setIsEOS();

    @Nullable String eosp2p$getPuid();
    @Nullable String eosp2p$getSocket();

    void eosp2p$setPuid(String puid);
    void eosp2p$setSocket(String socket);
}
