package io.szktas.eos.Util;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

import static io.szktas.eos.Config.DEDICATED_SERVER_SECRET;

public class ServerSocketNameSupplier implements ISocketNameProvider {
    @Override
    public String get() {
        MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException e) {
            throw new RuntimeException(e);
        }
        byte[] hash = digest.digest(DEDICATED_SERVER_SECRET.get().getBytes(StandardCharsets.UTF_8));

        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 16; i++) {
            String hex = Integer.toHexString(0xff & hash[i]);
            if (hex.length() == 1) sb.append('0');
            sb.append(hex);
        }
        return sb.toString();
    }
}
