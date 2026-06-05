package io.szktas.eos.Util;

import java.util.UUID;

public class ClientSocketNameSupplier implements ISocketNameProvider {
    @Override
    public String get() {
        return UUID.randomUUID().toString().replace("-", "");
    }
}
