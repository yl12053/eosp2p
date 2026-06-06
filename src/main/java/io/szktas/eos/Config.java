package io.szktas.eos;

import net.minecraftforge.common.ForgeConfigSpec;
import net.minecraftforge.fml.common.Mod;

import java.util.UUID;
import java.util.regex.Pattern;

// An example config class. This is not required, but it's a good idea to have one to keep your config organized.
// Demonstrates how to use Forge's config APIs
@Mod.EventBusSubscriber(modid = Main.MODID, bus = Mod.EventBusSubscriber.Bus.MOD)
public class Config
{
    private static final Pattern UUID_Pattern = Pattern.compile("^[0-9a-f]{32}$");
    private static final Pattern CRED_Pattern = Pattern.compile("^[0-9a-zA-Z]{32}$");
    private static final Pattern P_Pattern = Pattern.compile("^p-[0-9a-z]{30}$");
    private static final Pattern B_Pattern = Pattern.compile("^[0-9a-zA-Z+/]+$");
    private static final ForgeConfigSpec.Builder BUILDER = new ForgeConfigSpec.Builder();
    public static final ForgeConfigSpec.ConfigValue<String> PRODUCT_ID = BUILDER
            .comment("The Product ID of EOS Service")
            .define(
                    "product_id",
                    "8a1d4c95e02d4392885a4367867a37a8",
                    (v) -> v instanceof String s && UUID_Pattern.matcher(s).matches()
                );

    public static final ForgeConfigSpec.ConfigValue<String> SANDBOX_ID = BUILDER
            .comment("The Sandbox ID of EOS Service")
            .define(
                    "sandbox_id",
                    "4826e02ae3b147f5b8179331493e48d9",
                    (v) -> v instanceof String s && (UUID_Pattern.matcher(s).matches() || P_Pattern.matcher(s).matches())
            );

    public static final ForgeConfigSpec.ConfigValue<String> DEPLOY_ID = BUILDER
            .comment("The Deployment ID of EOS Service")
            .define(
                    "deploy_id",
                    "5fee7ea2b0474425bf3a15d55300cf05",
                    (v) -> v instanceof String s && UUID_Pattern.matcher(s).matches()
            );

    public static final ForgeConfigSpec.ConfigValue<String> CLIENT_CREDENTIAL = BUILDER
            .comment("The Client Credential of EOS Service")
            .define(
                    "client_crecential",
                    "xyza7891VgnLBKsMGcIBgBFjMlSrrBYP",
                    (v) -> v instanceof String s && CRED_Pattern.matcher(s).matches()
            );

    public static final ForgeConfigSpec.ConfigValue<String> CLIENT_SECRET = BUILDER
            .comment("The Client Credential of EOS Service")
            .define(
                    "client_secret",
                    "N83NQFeUPTDzeBZXqgSRcx0ENA+078yS3uyuWdI2xgE",
                    (v) -> v instanceof String s && B_Pattern.matcher(s).matches()
            );

    public static final ForgeConfigSpec.ConfigValue<String> DEDICATED_SERVER_SECRET = BUILDER
            .comment("Dedicated Server Only: random string to identify server")
            .define(
                    "server_secret",
                    UUID.randomUUID().toString()
            );

    public static final ForgeConfigSpec.DoubleValue TIMEOUT = BUILDER
            .comment("Timeout before disconnect to EOS Service, unit: second")
            .defineInRange(
                    "timeout",
                    5d,
                    0d,
                    Double.MAX_VALUE
            );

    public static final ForgeConfigSpec.BooleanValue SHOW_HINT_ON_LAUNCH = BUILDER
            .comment("Show hint on launch if any error occurs")
            .define("hint_on_launch", true);

    static final ForgeConfigSpec SPEC = BUILDER.build();
}
