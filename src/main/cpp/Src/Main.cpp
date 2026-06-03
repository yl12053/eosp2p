// g++ -O3 -march=x86-64 -fno-plt -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -g -I../Include -I"C:/Program Files/Microsoft/jdk-17.0.12.7-hotspot/include" -I"C:/Program Files/Microsoft/jdk-17.0.12.7-hotspot/include/win32" -L../Bin -std=c++20 -static-libgcc -static-libstdc++ -static -shared -l:../Bin/EOSSDK-Win64-Shipping.dll Main.cpp -o main.dll

#include <condition_variable>
#include <cstring>

#include "io_szktas_eos_EOSBinder_EOSNative.h"

#include <jni.h>

#include <eos_logging.h>
#include <eos_sdk.h>
#include <eos_connect.h>
#include <eos_p2p.h>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

static JavaVM* globalJVM = nullptr;

static jclass integerClass = nullptr;
static jmethodID integerValueOf = nullptr;

static jclass byteClass = nullptr;
static jmethodID byteValueOf = nullptr;

static jobject globalLoggingConsumer = nullptr;
static jmethodID globalBiConsumerMethod = nullptr;
static std::mutex callbackMutex;

static jobject globalNameSupplier = nullptr;
static jmethodID globalSupplierMethod = nullptr;
static std::mutex supplierMutex;

static jobject globalIncomingInfoConsumer = nullptr;
static jmethodID globalIncomingInfoMethod = nullptr;
static std::mutex incomingInfoMutex;

static jobject globalEstablishedInfoConsumer = nullptr;
static jmethodID globalEstablishedInfoMethod = nullptr;
static std::mutex establishedInfoMutex;

static jobject globalInterruptInfoConsumer = nullptr;
static jmethodID globalInterruptInfoMethod = nullptr;
static std::mutex interruptInfoMutex;

static jobject globalCloseInfoConsumer = nullptr;
static jmethodID globalCloseInfoMethod = nullptr;
static std::mutex closeInfoMutex;

static jobject globalReceiveConsumer = nullptr;
static jmethodID globalReceiveMethod = nullptr;
static char* globalReceivePUID = nullptr;
static std::mutex receiveMutex;

static EOS_HPlatform platformHandle = nullptr;
static EOS_HConnect connectHandle = nullptr;
static EOS_HP2P p2pHandle = nullptr;

static std::mutex platformMutex;

static std::mutex p2pOptMutex;

struct PlatformArg {
    double timeout;
    char* productID;
    char* clientcredid;
    char* clientsecret;
    char* sandboxid;
    char* deploymentid;
    jobject callback;
};

static EOS_NotificationId ConnectionRequestNotificationId = 0;
static EOS_NotificationId ConnectionEstablishedNotificationId = 0;
static EOS_NotificationId ConnectionInterruptedNotificationId = 0;
static EOS_NotificationId ConnectionClosedNotificationId = 0;

static std::thread* mainLoopThread = nullptr;
static std::mutex initMutex;
static std::condition_variable condIsPlatformArgPresent;
static PlatformArg* platformArgPointer = nullptr;

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#include <sys/resource.h>
#endif

static void SetThreadToHighPriority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#elif defined(__APPLE__) || defined(__linux__)
    setpriority(PRIO_PROCESS, 0, -5);
#endif
}

class ScopedEnv {
public:
    ScopedEnv(): jvm(globalJVM), env(nullptr), shouldDetach(false) {
        jint result = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8);

        if (result == JNI_EDETACHED) {
            if (jvm->AttachCurrentThread(reinterpret_cast<void**>(&env), nullptr) == JNI_OK) {
                shouldDetach = true;
            } else {
                env = nullptr;
            }
        }
    }

    ~ScopedEnv() {
        if (shouldDetach) {
            jvm->DetachCurrentThread();
        }
    }

    bool success() const { return env != nullptr; }
    operator JNIEnv*() const { return env; }
private:
    JavaVM* jvm;
    JNIEnv* env;
    bool shouldDetach;
};

std::string ToHex(unsigned char* data, uint32_t len) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint32_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

inline jobject BoxInt(JNIEnv*& env, const jint val) {
    return env->CallStaticObjectMethod(integerClass, integerValueOf, val);
}

inline jobject BoxByte(JNIEnv*& env, const jbyte val) {
    return env->CallStaticObjectMethod(byteClass, byteValueOf, val);
}

inline char* copyString(const char* src) {
    size_t len = strlen(src) + 1;
    char* newstr = static_cast<char*>(malloc(sizeof(char) * len));
    strcpy(newstr, src);
    return newstr;
}

inline void checkException(JNIEnv* env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

void EOS_CALL OnLogMessageReceived(const EOS_LogMessage* InMessage) {
    ScopedEnv envs;

    if (!envs.success()) return;
    JNIEnv* env = envs;

    jobject localLoggingConsumer;
    jmethodID localBiConsumerMethod;
    {
        std::lock_guard lock(callbackMutex);
        if (globalLoggingConsumer == nullptr) return;
        if (globalBiConsumerMethod == nullptr) return;
        localLoggingConsumer = env->NewLocalRef(globalLoggingConsumer);
        localBiConsumerMethod = globalBiConsumerMethod;
    }

    jobject statuscode = BoxInt(env, static_cast<int32_t>(InMessage->Level));

    std::string s = std::format("[{}] {}", InMessage->Category, InMessage->Message);
    const char* str = s.c_str();

    jstring jstr = env->NewStringUTF(str);

    env->CallVoidMethod(localLoggingConsumer, localBiConsumerMethod, statuscode, jstr);
    checkException(env);
    env->DeleteLocalRef(localLoggingConsumer);
    env->DeleteLocalRef(jstr);
}

struct ClientData {
    char* copyChars;
    jobject globalCallback;
    jmethodID methodID;
    char* nameIfPresent;
};

inline char* makeCharFromJString(JNIEnv*& env, jstring& str) {
    const char* srcChr = env->GetStringUTFChars(str, nullptr);
    size_t length = strlen(srcChr);
    char* newChar = static_cast<char*>(malloc(sizeof(char) * (length + 1)));
    strcpy(newChar, srcChr);
    env->ReleaseStringUTFChars(str, srcChr);
    return newChar;
}

inline char* getName(JNIEnv*& env) {
    jobject nameSupplier;
    jmethodID id;
    {
        std::lock_guard lock(supplierMutex);
        if (globalNameSupplier == nullptr) {
            char* ret = static_cast<char*>(malloc(sizeof(char) * 11));
            strcpy(ret, "Mod Player");
            return ret;
        }
        nameSupplier = env->NewLocalRef(globalNameSupplier);
        id = globalSupplierMethod;
    }
    jstring strj = static_cast<jstring>(env->CallObjectMethod(nameSupplier, id));
    checkException(env);
    char* rstr = makeCharFromJString(env, strj);
    env->DeleteLocalRef(nameSupplier);
    return rstr;
}

static void Login(ClientData* clientData, EOS_Connect_OnLoginCallback callback) {
    EOS_Connect_LoginOptions LoginOptions = {};
    LoginOptions.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;

    EOS_Connect_Credentials creds = {};
    creds.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
    creds.Type = EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    creds.Token = nullptr;

    LoginOptions.Credentials = &creds;
    EOS_Connect_UserLoginInfo LoginInfo = {};
    LoginInfo.ApiVersion = EOS_CONNECT_USERLOGININFO_API_LATEST;
    LoginInfo.NsaIdToken = nullptr;

    {
        ScopedEnv envs;
        JNIEnv* env = envs;
        if (envs.success()) {
            char* name = getName(env);
            LoginInfo.DisplayName = name;
            clientData->nameIfPresent = name;
        } else {
            LoginInfo.DisplayName = "Mod Player";
            clientData->nameIfPresent = nullptr;
        }
    }
    LoginOptions.UserLoginInfo = &LoginInfo;

    EOS_Connect_Login(connectHandle, &LoginOptions, clientData, callback);
}

static int CreatePlatform(double timeout, const char* productID, const char* clientcredid, const char* clientsecret, const char* sandboxid, const char* deploymentid) {
    std::lock_guard lock(platformMutex);
    if (connectHandle != nullptr) return 0;

    EOS_Platform_Options PlatformOptions = {};
    PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;
    PlatformOptions.bIsServer = EOS_FALSE;
    PlatformOptions.EncryptionKey = nullptr;
    PlatformOptions.Reserved = nullptr;
    PlatformOptions.SystemSpecificOptions = nullptr;
    PlatformOptions.OverrideCountryCode = nullptr;
    PlatformOptions.OverrideLocaleCode = nullptr;
    PlatformOptions.ProductId = productID;
    PlatformOptions.SandboxId = sandboxid;
    PlatformOptions.DeploymentId = deploymentid;
    PlatformOptions.ClientCredentials.ClientId = clientcredid;
    PlatformOptions.ClientCredentials.ClientSecret = clientsecret;
    PlatformOptions.Flags = EOS_PF_DISABLE_OVERLAY;
    PlatformOptions.TaskNetworkTimeoutSeconds = &timeout;
    PlatformOptions.TickBudgetInMilliseconds = 16;

    platformHandle = EOS_Platform_Create(&PlatformOptions);
    if (!platformHandle) {
        return 1;
    }

    connectHandle = EOS_Platform_GetConnectInterface(platformHandle);
    if (!connectHandle) {
        return 2;
    }

    p2pHandle = EOS_Platform_GetP2PInterface(platformHandle);
    if (!p2pHandle) {
        return 3;
    }

    EOS_P2P_SetPortRangeOptions SetPortRangeOpts = {};
    SetPortRangeOpts.ApiVersion = EOS_P2P_SETPORTRANGE_API_LATEST;
    SetPortRangeOpts.Port = 0;
    SetPortRangeOpts.MaxAdditionalPortsToTry = 0;
    EOS_P2P_SetPortRange(p2pHandle, &SetPortRangeOpts);

    EOS_P2P_SetPacketQueueSizeOptions PacketOpts = {};
    PacketOpts.ApiVersion = EOS_P2P_SETPACKETQUEUESIZE_API_LATEST;
    PacketOpts.IncomingPacketQueueMaxSizeBytes = 33554432;
    PacketOpts.OutgoingPacketQueueMaxSizeBytes = 33554432;
    EOS_P2P_SetPacketQueueSize(p2pHandle, &PacketOpts);

    EOS_Connect_AddNotifyAuthExpirationOptions NotifyAuthExpirationOptions = {};
    NotifyAuthExpirationOptions.ApiVersion = EOS_CONNECT_ADDNOTIFYAUTHEXPIRATION_API_LATEST;

    EOS_Connect_AddNotifyAuthExpiration(connectHandle, &NotifyAuthExpirationOptions, nullptr, [](const EOS_Connect_AuthExpirationCallbackInfo* data) {
        ClientData* datas = new ClientData();
        Login(datas, [](const EOS_Connect_LoginCallbackInfo* data) {
            ClientData* datas = static_cast<ClientData *>(data->ClientData);
            if (datas->nameIfPresent != nullptr) {
                free(datas->nameIfPresent);
            }
            delete datas;
        });
    });
    return 0;
}

inline EOS_P2P_SocketId* MakeSocketID(char* name) {
    if (name == nullptr) return nullptr;
    EOS_P2P_SocketId* socketId = new EOS_P2P_SocketId();
    socketId->ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    strncpy(socketId->SocketName, name, 32);
    socketId->SocketName[32] = 0;
    return socketId;
}

template <typename T>
concept NotificationOption = requires(T t)
{
    { t.ApiVersion } -> std::same_as<int32_t&>;
    { t.LocalUserId } -> std::same_as<EOS_ProductUserId&>;
    { t.SocketId } -> std::same_as<const EOS_P2P_SocketId*&>;
};

template <typename T>
using P2P_CallbackType = void(*)(const T *);

template <NotificationOption T, typename V>
using P2P_SubscribeFunc = EOS_NotificationId(*)(
    EOS_HP2P,
    const T*,
    void*,
    P2P_CallbackType<V>
);

template <typename T>
concept CallbackNotificationStruct = requires(T t)
{
    { t.LocalUserId } -> std::same_as<EOS_ProductUserId&>;
    { t.RemoteUserId } -> std::same_as<EOS_ProductUserId&>;
    { t.SocketId } -> std::same_as<const EOS_P2P_SocketId*&>;
};

template <int size>
struct Holder {
    char data[size];

    constexpr operator const char* () {
        return data;
    }
};

template <int inps>
constexpr Holder<4 + inps * 18> createMethodDesc() {
    constexpr char sig[] = "Ljava/lang/Object;";
    Holder<4 + inps * 18> buf = {};
    buf.data[0] = '(';
    for (int i = 0; i < inps; i++) {
        for (int j = 0; j < 18; j++) {
            buf.data[i * 18 + j + 1] = sig[j];
        }
    }
    buf.data[1 + inps * 18] = ')';
    buf.data[2 + inps * 18] = 'V';
    buf.data[3 + inps * 18] = '\0';
    return buf;
}

template<size_t N>
struct FixedString {
    char data[N];
    constexpr FixedString(const char (&str)[N]) {
        for(size_t i = 0; i < N; ++i) data[i] = str[i];
    }

    constexpr operator const char* () const {
        return data;
    }
};

template <std::mutex& lockMut, jobject& consumer, jmethodID& id, FixedString name, int inputCount>
static void setGenericCallback(JNIEnv* env, jobject consumerReplace) {
    static constexpr auto sig = createMethodDesc<inputCount>();

    std::lock_guard lock(lockMut);
    if (consumer != nullptr) {
        env->DeleteGlobalRef(consumer);
    }

    consumer = env->NewGlobalRef(consumerReplace);
    jclass clz = env->GetObjectClass(consumer);
    id = env->GetMethodID(clz, name, sig.data);
}

template <std::mutex& lockMutex, jobject& globalConsumer, jmethodID& globalMethodID>
struct Callback {
    template <CallbackNotificationStruct T>
    static void defaultMethod(const T* Data) {
        ScopedEnv envs;
        JNIEnv* env = envs;
        if (!envs.success()) return;
        jobject triconsumer;
        jmethodID trimethod;
        {
            std::lock_guard lock(lockMutex);
            if (globalConsumer == nullptr) return;
            triconsumer = env->NewLocalRef(globalConsumer);
            trimethod = globalMethodID;
        }
        char localpid[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
        char remotepid[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
        int32_t buffer_size = sizeof(localpid);
        EOS_ProductUserId_ToString(Data->LocalUserId, localpid, &buffer_size);
        EOS_ProductUserId_ToString(Data->RemoteUserId, remotepid, &buffer_size);

        jstring local = env->NewStringUTF(localpid);
        jstring remote = env->NewStringUTF(remotepid);
        jstring socket = Data->SocketId == nullptr ? nullptr : env->NewStringUTF(Data->SocketId->SocketName);

        env->CallVoidMethod(triconsumer, trimethod, local, remote, socket);
        checkException(env);

        env->DeleteLocalRef(local);
        env->DeleteLocalRef(remote);
        if (socket != nullptr) env->DeleteLocalRef(socket);
        env->DeleteLocalRef(triconsumer);
    }
};

template <NotificationOption T, CallbackNotificationStruct V>
static bool SubscribeP2PGenericRequest(JNIEnv* env, jstring puidj, jstring socketj, int32_t apiVersion,
    P2P_SubscribeFunc<T, V> func, P2P_CallbackType<V> callback, EOS_NotificationId& id) {
    char* puidstr = makeCharFromJString(env, puidj);
    char* name = socketj == nullptr ? nullptr : makeCharFromJString(env, socketj);
    EOS_ProductUserId puid = EOS_ProductUserId_FromString(puidstr);
    T Options = {};
    Options.ApiVersion = apiVersion;
    Options.LocalUserId = puid;
    Options.SocketId = MakeSocketID(name);

    id = func(p2pHandle, &Options, nullptr, callback);

    delete Options.SocketId;
    free(puidstr);
    if (name != nullptr) free(name);

    return id != EOS_INVALID_NOTIFICATIONID;
}

template <typename T>
struct is_char : std::disjunction<
    std::is_same<T, char*>,
    std::is_same<T, const char*>
> {};

template<typename... Args>
void callStringFunction(JNIEnv* env, jobject obj, jmethodID id, Args... arg) {
    static_assert((is_char<Args>::value && ...), "Not char*");
    constexpr size_t count = sizeof...(arg);
    jstring strs[count];

    size_t index = 0;
    ((strs[index++] = env->NewStringUTF(arg)), ...);
    env->CallVoidMethodA(obj, id, strs);
    checkException(env);
    for (size_t i = 0; i < count; i++) {
        env->DeleteLocalRef(strs[i]);
    }
}

template <typename T>
concept ConnectionOptions = requires(T t)
{
    { t.ApiVersion } -> std::same_as<int32_t&>;
    { t.LocalUserId } -> std::same_as<EOS_ProductUserId&>;
    { t.RemoteUserId } -> std::same_as<EOS_ProductUserId&>;
    { t.SocketId } -> std::same_as<const EOS_P2P_SocketId*&>;
};

template <ConnectionOptions T>
using ConnectionFunc = EOS_EResult(*)(
    EOS_HP2P,
    const T*
);

template <ConnectionOptions T>
static jstring doConnectionAction(JNIEnv* env, jstring localPUIDj, jstring remotePUIDj, jstring SocketIDj, int32_t apiVer, ConnectionFunc<T> func) {
    char* localPUIDs = makeCharFromJString(env, localPUIDj);
    char* remotePUIDs = makeCharFromJString(env, remotePUIDj);
    char* socketIDs = SocketIDj == nullptr ? nullptr : makeCharFromJString(env, SocketIDj);

    EOS_ProductUserId localPUID = EOS_ProductUserId_FromString(localPUIDs);
    EOS_ProductUserId remotePUID = EOS_ProductUserId_FromString(remotePUIDs);
    free(localPUIDs);
    free(remotePUIDs);

    if (EOS_ProductUserId_IsValid(remotePUID) == EOS_FALSE) {
        if (socketIDs != nullptr) free(socketIDs);
        return env->NewStringUTF("--remote_not_valid");
    }
    if (EOS_ProductUserId_IsValid(localPUID) == EOS_FALSE) {
        if (socketIDs != nullptr) free(socketIDs);
        return env->NewStringUTF("--local_not_valid");
    }

    T Options = {};
    Options.ApiVersion = apiVer;

    Options.LocalUserId = localPUID;
    Options.RemoteUserId = remotePUID;
    Options.SocketId = socketIDs == nullptr ? nullptr : MakeSocketID(socketIDs);

    EOS_EResult Result = func(p2pHandle, &Options);
    if (Result == EOS_EResult::EOS_Success) {
        if (Options.SocketId != nullptr) delete Options.SocketId;
        if (socketIDs != nullptr) free(socketIDs);
        return nullptr;
    }
    if (Options.SocketId != nullptr) delete Options.SocketId;
    if (socketIDs != nullptr) free(socketIDs);
    return env->NewStringUTF(EOS_EResult_ToString(Result));
}

inline void throwException(JNIEnv*& env, const char* msg, const char* clazz) {
    jclass exClass = env->FindClass(clazz == nullptr ? "java/lang/RuntimeException" : clazz);
    if (exClass == nullptr) {
        exClass = env->FindClass("java/lang/RuntimeException");
    }
    if (exClass != nullptr) {
        env->ThrowNew(exClass, msg);
    }
}

static bool tryReceive(JNIEnv*& env) {
    EOS_ProductUserId localPUID;
    jobject localConsumer;
    jmethodID localMethodID;
    {
        std::lock_guard lock(receiveMutex);
        if (globalReceiveConsumer == nullptr) return false;
        if (globalReceivePUID == nullptr) return false;
        localPUID = EOS_ProductUserId_FromString(globalReceivePUID);
        localConsumer = env->NewLocalRef(globalReceiveConsumer);
        localMethodID = globalReceiveMethod;
    }

    if (EOS_ProductUserId_IsValid(localPUID) == EOS_FALSE) {
        return false;
    }

    EOS_P2P_GetNextReceivedPacketSizeOptions SizeOpt = {};
    SizeOpt.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    SizeOpt.LocalUserId = localPUID;
    SizeOpt.RequestedChannel = nullptr;

    uint32_t nextSize;

    EOS_EResult SizeRet = EOS_P2P_GetNextReceivedPacketSize(p2pHandle, &SizeOpt, &nextSize);

    if (SizeRet == EOS_EResult::EOS_InvalidParameters) {
        return false;
    }
    if (SizeRet == EOS_EResult::EOS_NotFound) {
        return false;
    };

    EOS_P2P_ReceivePacketOptions ReceiveOptions = {};
    ReceiveOptions.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    ReceiveOptions.LocalUserId = localPUID;
    ReceiveOptions.MaxDataSizeBytes = nextSize;

    ReceiveOptions.RequestedChannel = nullptr;

    EOS_ProductUserId OutRemoteId = nullptr;
    EOS_P2P_SocketId OutSocketId = EOS_P2P_SocketId();
    uint8_t OutChannel = 0;

    uint32_t BytesWritten = 0;
    jbyte* OutMessage = static_cast<jbyte*>(malloc(nextSize));
    EOS_EResult ReceivePacketResult = EOS_P2P_ReceivePacket(p2pHandle, &ReceiveOptions, &OutRemoteId, &OutSocketId, &OutChannel, OutMessage, &BytesWritten);

    bool hasPacket = ReceivePacketResult == EOS_EResult::EOS_Success;

    if (hasPacket) {
        char remotePUID[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
        int32_t size = sizeof(remotePUID);
        EOS_ProductUserId_ToString(OutRemoteId, remotePUID, &size);
        jstring remotePUIDj = env->NewStringUTF(remotePUID);
        jstring sid = OutSocketId.SocketName == nullptr ? nullptr : env->NewStringUTF(OutSocketId.SocketName);
        jbyte channel = static_cast<jbyte>(OutChannel);
        jbyteArray arr = env->NewByteArray(BytesWritten);
        env->SetByteArrayRegion(arr, 0, BytesWritten, OutMessage);

        env->CallVoidMethod(localConsumer, localMethodID, remotePUIDj, sid, channel, arr);
        checkException(env);

        env->DeleteLocalRef(remotePUIDj);
        env->DeleteLocalRef(sid);
        env->DeleteLocalRef(arr);
    }

    env->DeleteLocalRef(localConsumer);
    free(OutMessage);

    return hasPacket;
}

extern "C" {
    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
        globalJVM = vm;

        JNIEnv* env;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) != JNI_OK) {
            return JNI_ERR;
        }

        jclass integer = env->FindClass("java/lang/Integer");
        integerClass = static_cast<jclass>(env->NewGlobalRef(integer));
        integerValueOf = env->GetStaticMethodID(integerClass, "valueOf", "(I)Ljava/lang/Integer;");

        jclass byte = env->FindClass("java/lang/Byte");
        byteClass = static_cast<jclass>(env->NewGlobalRef(byte));
        byteValueOf = env->GetStaticMethodID(byteClass, "valueOf", "(B)Ljava/lang/Byte;");

        return JNI_VERSION_1_8;
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_init(JNIEnv* env, jclass clazz, jstring name, jstring version, jobject consumer){;
        char* copyname = makeCharFromJString(env, name);
        char* copyver = makeCharFromJString(env, version);
        jobject consumerg = env->NewGlobalRef(consumer);
        jmethodID consumerid = env->GetMethodID(env->GetObjectClass(consumerg), "accept", "(Ljava/lang/Object;)V");

        mainLoopThread = new std::thread([copyname, copyver, consumerg, consumerid]() {
            SetThreadToHighPriority();
            ScopedEnv envs;
            if (!envs.success()) return;
            JNIEnv* env = envs;
            EOS_InitializeOptions EOSSdkOptions = {};
            EOSSdkOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;

            EOSSdkOptions.ProductName = copyname;
            EOSSdkOptions.ProductVersion = copyver;

            EOS_EResult InitResult = EOS_Initialize(&EOSSdkOptions);
            free(copyname);
            free(copyver);
            bool success = false;
            if (InitResult == EOS_EResult::EOS_Success) {
                EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_VeryVerbose);
                EOS_Logging_SetCallback(OnLogMessageReceived);
                env->CallVoidMethod(consumerg, consumerid, nullptr);
                checkException(env);
                success = true;
            } else {
                const char* error = EOS_EResult_ToString(InitResult);
                jstring str = env->NewStringUTF(error);
                env->CallVoidMethod(consumerg, consumerid, str);
                checkException(env);
                env->DeleteLocalRef(str);
            }
            env->DeleteGlobalRef(consumerg);

            {
                std::unique_lock lock(initMutex);
                condIsPlatformArgPresent.wait(lock, []{return platformArgPointer != nullptr;});
            }
            jmethodID consumerPlatformid = env->GetMethodID(env->GetObjectClass(platformArgPointer->callback), "accept", "(Ljava/lang/Object;)V");
            if (!success) {
                jobject retv = BoxInt(env, -1);
                env->CallVoidMethod(platformArgPointer->callback, consumerPlatformid, retv);
                checkException(env);
                env->DeleteLocalRef(retv);
                env->DeleteGlobalRef(platformArgPointer->callback);
                free(platformArgPointer);
                return;
            }
            int ret = CreatePlatform(
                platformArgPointer->timeout,
                platformArgPointer->productID,
                platformArgPointer->clientcredid,
                platformArgPointer->clientsecret,
                platformArgPointer->sandboxid,
                platformArgPointer->deploymentid
            );
            jobject retv = BoxInt(env, ret);
            env->CallVoidMethod(platformArgPointer->callback, consumerPlatformid, retv);
            checkException(env);
            env->DeleteGlobalRef(platformArgPointer->callback);
            env->DeleteLocalRef(retv);
            free(platformArgPointer->productID);
            free(platformArgPointer->clientcredid);
            free(platformArgPointer->clientsecret);
            free(platformArgPointer->sandboxid);
            free(platformArgPointer->deploymentid);
            delete platformArgPointer;
            platformArgPointer = nullptr;
            if (ret != 0) return;

            while (true) {
                if (platformHandle != nullptr) {
                    std::lock_guard lock(p2pOptMutex);
                    EOS_Platform_Tick(platformHandle);
                }
                while (tryReceive(env));
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        });

        mainLoopThread->detach();
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_SetNameGetter(JNIEnv* env, jclass clazz, jobject supplier) {
        std::lock_guard lock(supplierMutex);
        if (globalNameSupplier != nullptr) {
            env->DeleteGlobalRef(globalNameSupplier);
        }

        globalNameSupplier = env->NewGlobalRef(supplier);
        jclass clz = env->GetObjectClass(supplier);
        globalSupplierMethod = env->GetMethodID(clz, "get", "()Ljava/lang/Object;");
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_SetLogging(JNIEnv* env, jclass clazz, jobject consumer) {
        setGenericCallback<callbackMutex, globalLoggingConsumer, globalBiConsumerMethod, "accept", 2>(env, consumer);
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_getPUID(JNIEnv* env, jclass clazz, jstring uniqueID, jobject callback) {
        jclass clz = env->GetObjectClass(callback);
        jmethodID callbackmethod = env->GetMethodID(clz, "accept", "(Ljava/lang/Object;)V");

        jobject globalCallback = env->NewGlobalRef(callback);

        EOS_Connect_CreateDeviceIdOptions options = {};
        options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;

        const char* tempChars = env->GetStringUTFChars(uniqueID, nullptr);
        int length = strlen(tempChars);
        if (length > EOS_CONNECT_CREATEDEVICEID_DEVICEMODEL_MAX_LENGTH) {
            length = EOS_CONNECT_CREATEDEVICEID_DEVICEMODEL_MAX_LENGTH;
        }
        length++;
        char* copyChars = static_cast<char*>(malloc(sizeof(char) * length));
        snprintf(copyChars, length, "%s", tempChars);
        env->ReleaseStringUTFChars(uniqueID, tempChars);

        options.DeviceModel = copyChars;

        ClientData* client_data = static_cast<ClientData*>(malloc(sizeof(ClientData)));
        client_data->copyChars = copyChars;
        client_data->globalCallback = globalCallback;
        client_data->methodID = callbackmethod;

        EOS_Connect_CreateDeviceId(connectHandle, &options, client_data, [](const EOS_Connect_CreateDeviceIdCallbackInfo* data) {
            ScopedEnv envs;
            JNIEnv* env = envs;
            ClientData* client_data = static_cast<ClientData*>(data->ClientData);
            if (data->ResultCode == EOS_EResult::EOS_Success || data->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed) {
                Login(client_data, [](const EOS_Connect_LoginCallbackInfo* data) {
                    ScopedEnv envs;
                    JNIEnv* env = envs;
                    ClientData* client_data = static_cast<ClientData*>(data->ClientData);
                    if (data->ResultCode == EOS_EResult::EOS_Success) {
                        char buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
                        int32_t buffer_size = sizeof(buffer);
                        EOS_EResult result = EOS_ProductUserId_ToString(data->LocalUserId, buffer, &buffer_size);
                        if (result == EOS_EResult::EOS_Success) {
                            jstring str = env->NewStringUTF(buffer);
                            env->CallVoidMethod(client_data->globalCallback, client_data->methodID, str);
                            checkException(env);
                            env->DeleteLocalRef(str);
                        } else {
                            env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
                            checkException(env);
                        }
                        env->DeleteGlobalRef(client_data->globalCallback);
                        free(client_data->copyChars);
                        if (client_data->nameIfPresent != nullptr) free(client_data->nameIfPresent);
                        free(client_data);
                        return;
                    }
                    if (data->ResultCode == EOS_EResult::EOS_InvalidUser) {
                        EOS_Connect_CreateUserOptions CreateUserOptions = {};
                        CreateUserOptions.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
                        CreateUserOptions.ContinuanceToken = data->ContinuanceToken;
                        EOS_Connect_CreateUser(connectHandle, &CreateUserOptions, client_data, [](const EOS_Connect_CreateUserCallbackInfo* data) {
                            ScopedEnv envs;
                            JNIEnv* env = envs;
                            ClientData* client_data = static_cast<ClientData*>(data->ClientData);

                            if (data->ResultCode == EOS_EResult::EOS_Success) {
                                char buffer[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
                                int32_t buffer_size = sizeof(buffer);
                                EOS_EResult result = EOS_ProductUserId_ToString(data->LocalUserId, buffer, &buffer_size);
                                if (result == EOS_EResult::EOS_Success) {
                                    jstring str = env->NewStringUTF(buffer);
                                    env->CallVoidMethod(client_data->globalCallback, client_data->methodID, str);
                                    checkException(env);
                                    env->DeleteLocalRef(str);
                                } else {
                                    env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
                                    checkException(env);
                                }
                                env->DeleteGlobalRef(client_data->globalCallback);
                                free(client_data->copyChars);
                                if (client_data->nameIfPresent != nullptr) free(client_data->nameIfPresent);
                                free(client_data);
                                return;
                            }
                            if (EOS_EResult_IsOperationComplete(data->ResultCode) == EOS_FALSE) {
                                return;
                            }
                            env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
                            checkException(env);
                            env->DeleteGlobalRef(client_data->globalCallback);
                            free(client_data->copyChars);
                            if (client_data->nameIfPresent != nullptr) free(client_data->nameIfPresent);
                            free(client_data);
                        });
                        return;
                    }
                    if (EOS_EResult_IsOperationComplete(data->ResultCode) == EOS_FALSE) {
                        return;
                    }
                    env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
                    checkException(env);
                    env->DeleteGlobalRef(client_data->globalCallback);
                    free(client_data->copyChars);
                    if (client_data->nameIfPresent != nullptr) free(client_data->nameIfPresent);
                    free(client_data);
                });
                return;
            }
            if (EOS_EResult_IsOperationComplete(data->ResultCode) == EOS_FALSE) {
                return;
            }
            env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
            checkException(env);
            env->DeleteGlobalRef(client_data->globalCallback);
            free(client_data->copyChars);
            if (client_data->nameIfPresent != nullptr) free(client_data->nameIfPresent);
            free(client_data);
        });
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_initConnectionHandle(JNIEnv* env, jclass clazz, jdouble timeout, jstring pid, jstring credid, jstring secret, jstring sandbox, jstring depid, jobject callback) {
        const char* pidc = env->GetStringUTFChars(pid, nullptr);
        const char* credidc = env->GetStringUTFChars(credid, nullptr);
        const char* secretc = env->GetStringUTFChars(secret, nullptr);
        const char* sandboxc = env->GetStringUTFChars(sandbox, nullptr);
        const char* depc = env->GetStringUTFChars(depid, nullptr);

        char* pids = copyString(pidc);
        char* credids = copyString(credidc);
        char* secrets = copyString(secretc);
        char* sandboxs = copyString(sandboxc);
        char* deps = copyString(depc);

        {
            std::lock_guard lock(initMutex);
            platformArgPointer = new PlatformArg();
            platformArgPointer->timeout = timeout;
            platformArgPointer->productID = pids;
            platformArgPointer->clientcredid = credids;
            platformArgPointer->clientsecret = secrets;
            platformArgPointer->sandboxid = sandboxs;
            platformArgPointer->deploymentid = deps;
            platformArgPointer->callback = env->NewGlobalRef(callback);
        }
        condIsPlatformArgPresent.notify_one();

        env->ReleaseStringUTFChars(pid, pidc);
        env->ReleaseStringUTFChars(credid, credidc);
        env->ReleaseStringUTFChars(secret, secretc);
        env->ReleaseStringUTFChars(sandbox, sandboxc);
        env->ReleaseStringUTFChars(depid, depc);
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_setNetworkStatus(JNIEnv* env, jclass clazz, jint status) {
        if (platformHandle == nullptr) return;
        EOS_ENetworkStatus target;
        if (status == 0) {
            target = EOS_ENetworkStatus::EOS_NS_Online;
        } else if (status == 1) {
            target = EOS_ENetworkStatus::EOS_NS_Offline;
        } else {
            target = EOS_ENetworkStatus::EOS_NS_Disabled;
        }

        EOS_ENetworkStatus old = EOS_Platform_GetNetworkStatus(platformHandle);
        if (old != target) {
            EOS_Platform_SetNetworkStatus(platformHandle, target);
        }
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeIncomingConnectionRequestHandler(JNIEnv* env, jclass clazz, jobject triconsumer) {
        setGenericCallback<incomingInfoMutex, globalIncomingInfoConsumer, globalIncomingInfoMethod, "accept", 3>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeIncomingConnectionRequest(JNIEnv* env, jclass clazz, jstring puidj, jstring socketj) {
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionRequest,
            Callback<incomingInfoMutex, globalIncomingInfoConsumer, globalIncomingInfoMethod>::defaultMethod,
            ConnectionRequestNotificationId
        );
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeEstablishedConnectionRequestHandler(JNIEnv* env, jclass clazz, jobject triconsumer) {
        setGenericCallback<establishedInfoMutex, globalEstablishedInfoConsumer, globalEstablishedInfoMethod, "accept", 3>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeEstablishedConnectionRequest(JNIEnv* env, jclass clazz, jstring puidj, jstring socketj) {
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionEstablished,
            Callback<establishedInfoMutex, globalEstablishedInfoConsumer, globalEstablishedInfoMethod>::defaultMethod,
            ConnectionEstablishedNotificationId
        );
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeInterruptConnectionRequestHandler(JNIEnv* env, jclass clazz, jobject triconsumer) {
        setGenericCallback<interruptInfoMutex, globalInterruptInfoConsumer, globalInterruptInfoMethod, "accept", 3>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeInterruptConnectionRequest(JNIEnv* env, jclass clazz, jstring puidj, jstring socketj) {
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionInterrupted,
            Callback<interruptInfoMutex, globalInterruptInfoConsumer, globalInterruptInfoMethod>::defaultMethod,
            ConnectionInterruptedNotificationId
        );
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeCloseConnectionRequestHandler(JNIEnv* env, jclass clazz, jobject triconsumer) {
        setGenericCallback<closeInfoMutex, globalCloseInfoConsumer, globalCloseInfoMethod, "accept", 3>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeCloseConnectionRequest(JNIEnv* env, jclass clazz, jstring puidj, jstring socketj) {
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionClosed,
            Callback<closeInfoMutex, globalCloseInfoConsumer, globalCloseInfoMethod>::defaultMethod,
            ConnectionClosedNotificationId
        );
    };

    JNIEXPORT jstring JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_connectOrAccept(JNIEnv* env, jclass clazz, jstring localPUIDj, jstring remotePUIDj, jstring SocketIDj) {
        return doConnectionAction(env, localPUIDj, remotePUIDj, SocketIDj, EOS_P2P_ACCEPTCONNECTION_API_LATEST, EOS_P2P_AcceptConnection);
    };

    JNIEXPORT jstring JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_close(JNIEnv* env, jclass clazz, jstring localPUIDj, jstring remotePUIDj, jstring SocketIDj) {
        return doConnectionAction(env, localPUIDj, remotePUIDj, SocketIDj, EOS_P2P_CLOSECONNECTION_API_LATEST, EOS_P2P_CloseConnection);
    };

    JNIEXPORT jstring JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_send(JNIEnv* env, jclass clazz, jstring localPUIDj, jstring remotePUIDj, jstring socketIDj, jbyte channel, jbyteArray dataj) {
        char* localPUIDs = makeCharFromJString(env, localPUIDj);
        char* remotePUIDs = makeCharFromJString(env, remotePUIDj);
        char* socketIDs = socketIDj == nullptr ? nullptr : makeCharFromJString(env, socketIDj);

        EOS_ProductUserId localPUID = EOS_ProductUserId_FromString(localPUIDs);
        EOS_ProductUserId remotePUID = EOS_ProductUserId_FromString(remotePUIDs);
        free(localPUIDs);
        free(remotePUIDs);

        if (EOS_ProductUserId_IsValid(remotePUID) == EOS_FALSE) {
            if (socketIDs != nullptr) free(socketIDs);
            return env->NewStringUTF("--remote_not_valid");
        }
        if (EOS_ProductUserId_IsValid(localPUID) == EOS_FALSE) {
            if (socketIDs != nullptr) free(socketIDs);
            return env->NewStringUTF("--local_not_valid");
        }

        EOS_P2P_SendPacketOptions Options = {};
        Options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
        Options.LocalUserId = localPUID;
        Options.RemoteUserId = remotePUID;
        Options.SocketId = socketIDs == nullptr ? nullptr : MakeSocketID(socketIDs);
        Options.bAllowDelayedDelivery = EOS_TRUE;
        Options.Channel = static_cast<uint8_t>(channel);
        Options.Reliability = EOS_EPacketReliability::EOS_PR_ReliableOrdered;
        Options.bDisableAutoAcceptConnection = EOS_TRUE;
        jbyte* bytes = env->GetByteArrayElements(dataj, nullptr);
        uint32_t len = static_cast<uint32_t>(env->GetArrayLength(dataj)) * sizeof(jbyte);

        EOS_EResult SendPacketResult;

        int j = 0;

        {
            std::lock_guard lock(p2pOptMutex);
            for (uint32_t i = 0; i < len; i = i + 1024) {
                j++;
                jbyte* bytesStart = bytes + i;
                uint32_t size = len - i > 1024 ? 1024 : len - i;

                Options.DataLengthBytes = size;
                Options.Data = bytesStart;
                SendPacketResult = EOS_P2P_SendPacket(p2pHandle, &Options);

                if (SendPacketResult != EOS_EResult::EOS_Success) break;
            }
        }

        env->ReleaseByteArrayElements(dataj, bytes, JNI_ABORT);
        if (Options.SocketId != nullptr) delete Options.SocketId;
        if (socketIDs != nullptr) free(socketIDs);

        if (SendPacketResult == EOS_EResult::EOS_Success) return nullptr;

        const char* errorCode = EOS_EResult_ToString(SendPacketResult);
        return env->NewStringUTF(errorCode);
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_registerReceiveCallbackFor(JNIEnv* env, jclass clazz, jstring localPUIDj, jobject consumer) {
        std::lock_guard lock(receiveMutex);
        if (globalReceiveConsumer != nullptr) {
            env->DeleteGlobalRef(globalReceiveConsumer);
        }
        if (globalReceivePUID != nullptr) free(globalReceivePUID);

        globalReceiveConsumer = env->NewGlobalRef(consumer);
        jclass clz = env->GetObjectClass(consumer);
        globalReceiveMethod = env->GetMethodID(clz, "accept", "(Ljava/lang/String;Ljava/lang/String;B[B)V");
        globalReceivePUID = makeCharFromJString(env, localPUIDj);
    };
}