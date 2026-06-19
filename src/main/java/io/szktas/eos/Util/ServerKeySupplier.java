package io.szktas.eos.Util;

import io.szktas.eos.Config;

import java.util.UUID;

import static io.szktas.eos.Config.RANDOM_CONNECT_STRING;

public class ServerKeySupplier implements IKeySupplier{
    @Override public String getBaseKey() {
        return Config.DEDICATED_SERVER_SECRET.get() + (RANDOM_CONNECT_STRING.get() ? UUID.randomUUID().toString() : "");
    }
}
