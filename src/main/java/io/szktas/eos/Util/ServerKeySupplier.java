package io.szktas.eos.Util;

import io.szktas.eos.Config;

public class ServerKeySupplier implements IKeySupplier{
    @Override public String getBaseKey() {
        return Config.DEDICATED_SERVER_SECRET.get();
    }
}
