package io.szktas.eos.Network;

public record ConnectionKey(String remotePUID, String socketID, byte channelID) {
}
