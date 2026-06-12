// clang++ -static -O3 -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wstack-usage=1024 -Wformat=2 -Wconversion -march=x86-64 -fno-plt -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -g3 -I../Include -I"C:/Program Files/Microsoft/jdk-17.0.12.7-hotspot/include" -I"C:/Program Files/Microsoft/jdk-17.0.12.7-hotspot/include/win32" -L../Bin -std=c++20 -static-libgcc -static-libstdc++ -shared -l:../Bin/EOSSDK-Win64-Shipping.dll Main.cpp kcp/ikcp.c -o main.dll

#include <condition_variable>
#include <cstring>

#include "io_szktas_eos_EOSBinder_EOSNative.h"

#include <jni.h>

#include <eos_logging.h>
#include <eos_sdk.h>
#include <eos_connect.h>
#include <eos_p2p.h>
#include <future>
#include <queue>
#include <shared_mutex>

#ifdef __APPLE__
#define FMT_HEADER_ONLY
#include <fmt/core.h>
#include <fmt/format.h>
namespace fmtns = fmt;
#else
#include <format>
namespace fmtns = std;
#endif


#include <functional>
#include <iomanip>
#include <mutex>
#include <string>
#include <thread>

#include "ska/flat_hash_map.hpp"
#include "magic_enum/include/magic_enum/magic_enum.hpp"

#include "cppcrc/cppcrc.h"

extern "C" {
#include "kcp/ikcp.h"
}

class TaskQueue {
public:
    void Push(std::function<void()> task) {
        std::lock_guard lock(mutex_);
        tasks_.push(std::move(task));
    }

    std::queue<std::function<void()>> PopAll() {
        std::queue<std::function<void()>> local_tasks;
        {
            std::lock_guard lock(mutex_);
            std::swap(local_tasks, tasks_);
        }
        return local_tasks;
    }

    std::future<void*> Run(std::function<void*()> func) {
        auto task_ptr = std::make_shared<std::packaged_task<void*()>>(std::move(func));

        std::future<void*> fut = task_ptr->get_future();

        Push([task_ptr]() {
            (*task_ptr)();
        });

        return fut;
    }

private:
    std::mutex mutex_;
    std::queue<std::function<void()>> tasks_;
};

TaskQueue EOSQueue;

#define FNV_OFFSET_BASIS 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull
inline uint64_t FNV_1A(uint64_t& hash, const char* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        hash ^= static_cast<unsigned char>(*(data + i) & 0xff);
        hash *= FNV_PRIME;
    }

    return hash;
}

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
static std::thread* ikcpthread = nullptr;
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
#ifdef __ANDROID__
#define PRIORITY_AS -10
#else
#define PRIORITY_AS -5
#endif
    setpriority(PRIO_PROCESS, 0, PRIORITY_AS);
#endif
}

std::atomic isShutdown(false);

class ScopedFrame {
public:
    ScopedFrame(JNIEnv* envs, int cap): env(envs) {
        shouldRelease = (env->PushLocalFrame(cap) == JNI_OK);
    }

    ~ScopedFrame() {
        if (shouldRelease) {
            env->PopLocalFrame(nullptr);
        }
    }
private:
    bool shouldRelease;
    JNIEnv* env;
};

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

inline void checkException(JNIEnv* env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

inline jobject BoxInt(JNIEnv*& env, const jint val) {
    jobject ret = env->CallStaticObjectMethod(integerClass, integerValueOf, val);
    checkException(env);
    return ret;
}

inline jobject BoxByte(JNIEnv*& env, const jbyte val) {
    jobject ret = env->CallStaticObjectMethod(byteClass, byteValueOf, val);
    checkException(env);
    return ret;
}

inline char* copyString(const char* src) {
    size_t len = strlen(src) + 1;
    char* newstr = new char[sizeof(char) * len];
    strcpy(newstr, src);
    return newstr;
}

void EOS_CALL OnLogMessageReceived(const EOS_LogMessage* InMessage) {
    if (isShutdown.load()) return;
    ScopedEnv envs;

    if (!envs.success()) return;
    JNIEnv* env = envs;

    ScopedFrame frame(env, 256);

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

    std::string s = fmtns::format("[{}] {}", InMessage->Category, InMessage->Message);
    const char* str = s.c_str();

    jstring jstr = env->NewStringUTF(str);

    env->CallVoidMethod(localLoggingConsumer, localBiConsumerMethod, statuscode, jstr);
    checkException(env);
    env->DeleteLocalRef(localLoggingConsumer);
    env->DeleteLocalRef(jstr);
}

static void Log(int level, const char* InMessage) {
    if (isShutdown.load()) return;
    ScopedEnv envs;

    if (!envs.success()) return;
    JNIEnv* env = envs;

    ScopedFrame frame(env, 256);

    jobject localLoggingConsumer;
    jmethodID localBiConsumerMethod;
    {
        std::lock_guard lock(callbackMutex);
        if (globalLoggingConsumer == nullptr) return;
        if (globalBiConsumerMethod == nullptr) return;
        localLoggingConsumer = env->NewLocalRef(globalLoggingConsumer);
        localBiConsumerMethod = globalBiConsumerMethod;
    }

    jstring jstr = env->NewStringUTF(InMessage);

    jobject statuscode = BoxInt(env, level);

    env->CallVoidMethod(localLoggingConsumer, localBiConsumerMethod, statuscode, jstr);
    checkException(env);
    env->DeleteLocalRef(localLoggingConsumer);
    env->DeleteLocalRef(jstr);
    env->DeleteLocalRef(statuscode);
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
    char* newChar = new char[sizeof(char) * (length + 1)];
    strcpy(newChar, srcChr);
    env->ReleaseStringUTFChars(str, srcChr);
    return newChar;
}

inline char* getName(JNIEnv*& env) {
    ScopedFrame frame(env, 256);
    jobject nameSupplier;
    jmethodID id;
    {
        std::lock_guard lock(supplierMutex);
        if (globalNameSupplier == nullptr) {
            char* ret = new char[sizeof(char) * 11];
            strcpy(ret, "Mod Player");
            return ret;
        }
        nameSupplier = env->NewLocalRef(globalNameSupplier);
        id = globalSupplierMethod;
    }
    jstring strj = std::launder(static_cast<jstring>(env->CallObjectMethod(nameSupplier, id)));
    checkException(env);
    char* rstr = makeCharFromJString(env, strj);
    env->DeleteLocalRef(nameSupplier);
    env->DeleteLocalRef(strj);
    return rstr;
}

static void Login(ClientData* clientData, EOS_Connect_OnLoginCallback callback) {
    if (isShutdown.load()) return;
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
    if (isShutdown.load()) return 1;
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
        if (isShutdown.load()) return;
        // ClientData* datas = new ClientData();
        auto datas = std::make_unique<ClientData>();
        Login(datas.release(), [](const EOS_Connect_LoginCallbackInfo* data) {
            std::unique_ptr<ClientData> datas(std::launder(static_cast<ClientData *>(data->ClientData)));
            if (datas->nameIfPresent != nullptr) {
                delete[] datas->nameIfPresent;
            }
            // delete datas;
        });
    });
    return 0;
}

inline EOS_P2P_SocketId* MakeSocketID(char* name) {
    if (isShutdown.load()) return nullptr;
    EOS_P2P_SocketId* socketId = new EOS_P2P_SocketId();
    socketId->ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    if (name == nullptr) {
        memset(socketId->SocketName, 0, sizeof(socketId->SocketName));
    } else {
        strncpy(socketId->SocketName, name, 32);
    }
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

    constexpr operator const char* () const {
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
    env->DeleteLocalRef(clz);
}

template <std::mutex& lockMutex, jobject& globalConsumer, jmethodID& globalMethodID>
struct Callback {
    template <CallbackNotificationStruct T>
    static void defaultMethod(const T* Data) {
        if (isShutdown.load()) return;
        ScopedEnv envs;
        JNIEnv* env = envs;
        if (!envs.success()) return;
        ScopedFrame frame(envs, 256);
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
        jstring socket = Data->SocketId->SocketName[0] == '\0' ? nullptr : env->NewStringUTF(Data->SocketId->SocketName);

        env->CallVoidMethod(triconsumer, trimethod, local, remote, socket);
        checkException(env);

        env->DeleteLocalRef(local);
        env->DeleteLocalRef(remote);
        if (socket != nullptr) env->DeleteLocalRef(socket);
        env->DeleteLocalRef(triconsumer);
    }

    static void closeMethod(const EOS_P2P_OnRemoteConnectionClosedInfo* Data) {
        if (isShutdown.load()) return;
        ScopedEnv envs;
        JNIEnv* env = envs;
        if (!envs.success()) return;
        ScopedFrame frame(envs, 256);
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
        jstring socket = Data->SocketId->SocketName[0] == '\0' ? nullptr : env->NewStringUTF(Data->SocketId->SocketName);
        auto reason = magic_enum::enum_name(Data->Reason);
        auto str = std::string(reason);
        jstring reasonz = env->NewStringUTF(str.c_str());

        env->CallVoidMethod(triconsumer, trimethod, local, remote, socket, reasonz);
        checkException(env);

        env->DeleteLocalRef(local);
        env->DeleteLocalRef(remote);
        env->DeleteLocalRef(reasonz);
        if (socket != nullptr) env->DeleteLocalRef(socket);
        env->DeleteLocalRef(triconsumer);
    }
};

template <NotificationOption T, CallbackNotificationStruct V>
static bool SubscribeP2PGenericRequest(JNIEnv* env, jstring puidj, jstring socketj, int32_t apiVersion,
    P2P_SubscribeFunc<T, V> func, P2P_CallbackType<V> callback, EOS_NotificationId& id) {
    if (isShutdown.load()) return false;
    char* puidstr = makeCharFromJString(env, puidj);
    char* name = socketj == nullptr ? nullptr : makeCharFromJString(env, socketj);
    EOS_ProductUserId puid = EOS_ProductUserId_FromString(puidstr);
    T Options = {};
    Options.ApiVersion = apiVersion;
    Options.LocalUserId = puid;
    Options.SocketId = MakeSocketID(name);

    id = func(p2pHandle, &Options, nullptr, callback);

    delete Options.SocketId;
    delete[] puidstr;
    if (name != nullptr) delete[] name;

    return id != EOS_INVALID_NOTIFICATIONID;
}

template <typename T>
struct is_char : std::disjunction<
    std::is_same<T, char*>,
    std::is_same<T, const char*>
> {};

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
    if (isShutdown.load()) return env->NewStringUTF("--shutdown");
    char* localPUIDs = makeCharFromJString(env, localPUIDj);
    char* remotePUIDs = makeCharFromJString(env, remotePUIDj);
    char* socketIDs = SocketIDj == nullptr ? nullptr : makeCharFromJString(env, SocketIDj);

    EOS_ProductUserId localPUID = EOS_ProductUserId_FromString(localPUIDs);
    EOS_ProductUserId remotePUID = EOS_ProductUserId_FromString(remotePUIDs);
    delete[] localPUIDs;
    delete[] remotePUIDs;

    if (EOS_ProductUserId_IsValid(remotePUID) == EOS_FALSE) {
        if (socketIDs != nullptr) delete[] socketIDs;
        return env->NewStringUTF("--remote_not_valid");
    }
    if (EOS_ProductUserId_IsValid(localPUID) == EOS_FALSE) {
        if (socketIDs != nullptr) delete[] socketIDs;
        return env->NewStringUTF("--local_not_valid");
    }

    T Options = {};
    Options.ApiVersion = apiVer;

    Options.LocalUserId = localPUID;
    Options.RemoteUserId = remotePUID;
    Options.SocketId = MakeSocketID(socketIDs);

    auto fut = EOSQueue.Run([f = std::move(func), o = std::move(Options)] (){
        EOS_EResult* vpot = new EOS_EResult;
        *vpot = f(p2pHandle, &o);
        return static_cast<void*>(vpot);
    });
    EOS_EResult* Result = static_cast<EOS_EResult*>(fut.get());

    jstring returnval = *Result == EOS_EResult::EOS_Success ? nullptr : env->NewStringUTF(EOS_EResult_ToString(*Result));
    delete Result;

    if (Options.SocketId != nullptr) delete Options.SocketId;
    if (socketIDs != nullptr) delete[] socketIDs;
    return returnval;
}

constexpr uint64_t MAGIC_NUM_1 = 0x17b7cd11f9a12c6ull;
constexpr uint64_t MAGIC_NUM_2 = 0x71002c434b0af32dull;

struct MASK {
    std::array<char, 8> data;
    constexpr MASK(uint64_t val) : data {
        static_cast<char>(val >> 0),
        static_cast<char>(val >> 8),
        static_cast<char>(val >> 16),
        static_cast<char>(val >> 24),
        static_cast<char>(val >> 32),
        static_cast<char>(val >> 40),
        static_cast<char>(val >> 48),
        static_cast<char>(val >> 56)
    } {}
};

constexpr auto MAGIC_1 = MASK(MAGIC_NUM_1).data;
constexpr auto MAGIC_2 = MASK(MAGIC_NUM_2).data;

inline uint64_t CalculateHash1(char* localPUID, char* remotePUID, char* socketID, char channel) {
    uint64_t hash = FNV_OFFSET_BASIS;

    FNV_1A(hash, localPUID, strlen(localPUID));
    FNV_1A(hash, remotePUID, strlen(remotePUID));
    if (socketID != nullptr) FNV_1A(hash, socketID, strlen(socketID));
    FNV_1A(hash, &channel, 1);
    FNV_1A(hash, MAGIC_1.data(), 8);

    return hash;
}

inline uint64_t CalculateHash2(char* localPUID, char* remotePUID, char* socketID, char channel) {
    uint64_t hash = FNV_OFFSET_BASIS;

    FNV_1A(hash, MAGIC_2.data(), 8);
    FNV_1A(hash, &channel, 1);
    if (socketID != nullptr) FNV_1A(hash, socketID, strlen(socketID));
    FNV_1A(hash, remotePUID, strlen(remotePUID));
    FNV_1A(hash, localPUID, strlen(localPUID));

    return hash;
}

struct Fingerprint {
    uint64_t h1;
    uint64_t h2;
    bool operator==(const Fingerprint& o) const {
        return h1 == o.h1 && h2 == o.h2;
    }
};

struct FHash {
    size_t operator()(const Fingerprint& f) const {
        return f.h1 ^ f.h2;
    }
};

struct IKCPData {
    char* localPUID;
    char* remotePUID;
    char* socketID;
    char channel;

    std::shared_ptr<std::mutex> mutex;
};

std::shared_mutex lock_ikcp;
ska::flat_hash_map<Fingerprint, ikcpcb*, FHash> IKCPs;

constexpr int IKCP_INTERVAL = 10;

inline void uint32_to_bytes_be(uint32_t val, char* out) {
    out[0] = (val >> 24) & 0xff;
    out[1] = (val >> 16) & 0xff;
    out[2] = (val >> 8) & 0xff;
    out[3] = val & 0xff;
}

inline uint32_t bytes_to_uint32_be(char* in) {
    uint32_t r = 0;
    r |= static_cast<uint32_t>(static_cast<uint8_t>(in[3] & 0xff));
    r |= static_cast<uint32_t>(static_cast<uint8_t>(in[2] & 0xff)) << 8;
    r |= static_cast<uint32_t>(static_cast<uint8_t>(in[1] & 0xff)) << 16;
    r |= static_cast<uint32_t>(static_cast<uint8_t>(in[0] & 0xff)) << 24;
    return r;
}

static int sendIKCPData(const char* bufraw, int lenraw, ikcpcb *kcp, void* user) {
    int len = lenraw + 4;
    char* buf = new char[len];
    memcpy(buf + 4, bufraw, lenraw);

    uint32_t crc = CRC32::C::calc(std::launder(reinterpret_cast<const uint8_t*>(bufraw)), lenraw);
    uint32_to_bytes_be(crc, buf);

    IKCPData* data = std::launder(static_cast<IKCPData*>(user));
    EOS_ProductUserId localPUID = EOS_ProductUserId_FromString(data->localPUID);
    EOS_ProductUserId remotePUID = EOS_ProductUserId_FromString(data->remotePUID);

    EOS_P2P_SendPacketOptions Options = {};
    Options.ApiVersion = EOS_P2P_SENDPACKET_API_LATEST;
    Options.LocalUserId = localPUID;
    Options.RemoteUserId = remotePUID;
    Options.SocketId = MakeSocketID(data->socketID);
    Options.bAllowDelayedDelivery = EOS_TRUE;
    Options.Channel = static_cast<uint8_t>(data->channel);
    Options.Reliability = EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
    Options.bDisableAutoAcceptConnection = EOS_TRUE;
    Options.DataLengthBytes = len;
    Options.Data = buf;

    EOS_EResult result;

    {
        std::lock_guard lock(p2pOptMutex);
        result = EOS_P2P_SendPacket(p2pHandle, &Options);
    }

    delete Options.SocketId;
    delete[] buf;

    if (result != EOS_EResult::EOS_Success) {
        Log(0, fmtns::format("Send packet not success: {}",  EOS_EResult_ToString(result)).c_str());
        return -1;
    }
    return 0;
}

bool tryReceiveFromEOS() {
    if (isShutdown.load()) return false;
    EOS_ProductUserId localPUID;
    std::unique_ptr<char[]> localPUIDs(new char[EOS_PRODUCTUSERID_MAX_LENGTH + 1]);
    {
        std::lock_guard lock(receiveMutex);
        if (globalReceivePUID == nullptr) return false;
        localPUID = EOS_ProductUserId_FromString(globalReceivePUID);
        strcpy(localPUIDs.get(), globalReceivePUID);
    }

    if (EOS_ProductUserId_IsValid(localPUID) == EOS_FALSE) {
        return false;
    }

    EOS_P2P_GetNextReceivedPacketSizeOptions SizeOpt = {};
    SizeOpt.ApiVersion = EOS_P2P_GETNEXTRECEIVEDPACKETSIZE_API_LATEST;
    SizeOpt.LocalUserId = localPUID;
    SizeOpt.RequestedChannel = nullptr;

    uint32_t nextSize;

    EOS_EResult SizeRet;
    {
        std::lock_guard lock(p2pOptMutex);
        SizeRet = EOS_P2P_GetNextReceivedPacketSize(p2pHandle, &SizeOpt, &nextSize);
    }

    if (SizeRet != EOS_EResult::EOS_Success) {
        return false;
    }

    EOS_P2P_ReceivePacketOptions ReceiveOptions = {};
    ReceiveOptions.ApiVersion = EOS_P2P_RECEIVEPACKET_API_LATEST;
    ReceiveOptions.LocalUserId = localPUID;
    ReceiveOptions.MaxDataSizeBytes = nextSize;

    ReceiveOptions.RequestedChannel = nullptr;

    EOS_ProductUserId OutRemoteId = nullptr;
    EOS_P2P_SocketId OutSocketId = EOS_P2P_SocketId();
    uint8_t OutChannel = 0;

    uint32_t BytesWritten = 0;
    std::unique_ptr<char[]> OutMessage(new char[nextSize]);

    EOS_EResult ReceivePacketResult;

    {
        std::lock_guard lock(p2pOptMutex);
        ReceivePacketResult = EOS_P2P_ReceivePacket(p2pHandle, &ReceiveOptions, &OutRemoteId, &OutSocketId, &OutChannel, OutMessage.get(), &BytesWritten);
    }

    if (ReceivePacketResult == EOS_EResult::EOS_Success) {
        const char* rawData = OutMessage.get() + 4;
        uint32_t realSize = BytesWritten - 4;
        if (realSize <= 0) {
            Log(0, "Message too small, without checksum");
            return false;
        }
        uint32_t checksumGiven = bytes_to_uint32_be(OutMessage.get());
        uint32_t calculateChecksum = CRC32::C::calc(reinterpret_cast<const uint8_t*>(rawData), realSize);
        if (checksumGiven != calculateChecksum) {
            Log(0, "Checksum failed");
            return false;
        }

        char remotePUID[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
        int32_t size = sizeof(remotePUID);
        EOS_ProductUserId_ToString(OutRemoteId, remotePUID, &size);

        uint64_t hash1 = CalculateHash1(localPUIDs.get(), remotePUID, OutSocketId.SocketName, OutChannel);
        uint64_t hash2 = CalculateHash2(localPUIDs.get(), remotePUID, OutSocketId.SocketName, OutChannel);
        Fingerprint fprint = { hash1, hash2 };
        {
            std::shared_lock lock(lock_ikcp);
            auto it = IKCPs.find(fprint);
            if (it != IKCPs.end()) {
                ikcpcb* kcp = it->second;
                IKCPData* data = std::launder(static_cast<IKCPData*>(kcp->user));
                auto mtx_ptr = data->mutex;
                {
                    std::lock_guard mlock(*mtx_ptr);
                    ikcp_input(kcp, rawData, realSize);
                }
            } else {
                Log(0, "Receive message, but not related to KCP");
            };
        }
    }

    return ReceivePacketResult == EOS_EResult::EOS_Success;
}

static ikcpcb* CreateIKCPFor(char* localPUID, char* remotePUID, char* socketID, char channel) {
    uint64_t hash1 = CalculateHash1(localPUID, remotePUID, socketID, channel);
    uint64_t hash2 = CalculateHash2(localPUID, remotePUID, socketID, channel);

    Fingerprint fprint = { hash1, hash2 };

    {
        std::unique_lock lock(lock_ikcp);

        auto it = IKCPs.find(fprint);
        if (it != IKCPs.end()) return it->second;
        char* localPUIDcopy = copyString(localPUID);
        char* remotePUIDcopy = copyString(remotePUID);
        char* socketIDcopy = copyString(socketID);
        IKCPData* userdata = new IKCPData();
        userdata->localPUID = localPUIDcopy;
        userdata->remotePUID = remotePUIDcopy;
        userdata->socketID = socketIDcopy;
        userdata->channel = channel;
        userdata->mutex = std::make_shared<std::mutex>();
        ikcpcb* kcp = ikcp_create(0, userdata);
        kcp->output = sendIKCPData;
        kcp->stream = 1;
        ikcp_nodelay(kcp, 1, IKCP_INTERVAL, 2, 1);
        ikcp_setmtu(kcp, 1024);
        ikcp_wndsize(kcp, 128, 128);

        IKCPs[fprint] = kcp;
        return kcp;
    }
}

static void RemoveIKCPFor(char* localPUID, char* remotePUID, char* socketID, char channel) {
    uint64_t hash1 = CalculateHash1(localPUID, remotePUID, socketID, channel);
    uint64_t hash2 = CalculateHash2(localPUID, remotePUID, socketID, channel);

    Fingerprint fprint = { hash1, hash2 };

    {
        std::unique_lock lock(lock_ikcp);
        auto it = IKCPs.find(fprint);
        if (it != IKCPs.end()) {
            IKCPData* userdata = std::launder(static_cast<IKCPData*>(it->second->user));
            auto mtx_ptr = userdata->mutex;
            {
                std::lock_guard mlock(*mtx_ptr);
                ikcp_release(it->second);
                IKCPs.erase(it);
                delete[] userdata->localPUID;
                delete[] userdata->remotePUID;
                delete[] userdata->socketID;
            }
            delete userdata;
        }
    }
}

static void Teardown() {
    bool expected = false;
    if (isShutdown.compare_exchange_strong(expected, true, std::memory_order_seq_cst, std::memory_order_relaxed)) {
        EOS_Shutdown();
    }
}

class DynBuffer {
public:
    DynBuffer(unsigned int size) {
        p = static_cast<char*>(malloc(size));
        s = size;
    }

    void reserveIfSmaller(unsigned int size) {
        if (size < s) return;

        unsigned int need_size = std::bit_ceil(size);

        p = static_cast<char*>(realloc(p, need_size));
        s = need_size;
    }

    char* get() {
        return p;
    }

    unsigned int size() {
        return s;
    }

    operator char* () const {
        return p;
    }

    const jbyte* getBuf()  {
        return reinterpret_cast<const jbyte*>(p);
    }

    ~DynBuffer() {
        free(p);
    }

    DynBuffer(const DynBuffer&) = delete;
    DynBuffer& operator=(const DynBuffer&) = delete;
private:
    char* p = nullptr;
    unsigned int s = 0;
};

DynBuffer buffer(32768);


extern "C" {
    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
        globalJVM = vm;

        JNIEnv* env;
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_8) != JNI_OK) {
            return JNI_ERR;
        }

        jclass integer = env->FindClass("java/lang/Integer");
        integerClass = static_cast<jclass>(env->NewGlobalRef(integer));
        integerValueOf = env->GetStaticMethodID(integerClass, "valueOf", "(I)Ljava/lang/Integer;");
        env->DeleteLocalRef(integer);

        jclass byte = env->FindClass("java/lang/Byte");
        byteClass = static_cast<jclass>(env->NewGlobalRef(byte));
        byteValueOf = env->GetStaticMethodID(byteClass, "valueOf", "(B)Ljava/lang/Byte;");
        env->DeleteLocalRef(byte);

        return JNI_VERSION_1_8;
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_init(JNIEnv* env, jclass, jstring name, jstring version, jobject consumer){;
        if (isShutdown.load()) return;
        ScopedFrame frame(env, 256);
        char* copyname = makeCharFromJString(env, name);
        char* copyver = makeCharFromJString(env, version);
        jobject consumerg = env->NewGlobalRef(consumer);
        jclass clazz = env->GetObjectClass(consumerg);
        jmethodID consumerid = env->GetMethodID(clazz, "accept", "(Ljava/lang/Object;)V");
        env->DeleteLocalRef(clazz);

        mainLoopThread = new std::thread([copyname, copyver, consumerg, consumerid]() {
            SetThreadToHighPriority();
            ScopedEnv envs;
            if (!envs.success()) return;
            JNIEnv* env = envs;
            ScopedFrame frame(env, 256);
            EOS_InitializeOptions EOSSdkOptions = {};
            EOSSdkOptions.ApiVersion = EOS_INITIALIZE_API_LATEST;

            EOSSdkOptions.ProductName = copyname;
            EOSSdkOptions.ProductVersion = copyver;

            EOS_EResult InitResult = EOS_Initialize(&EOSSdkOptions);
            delete[] copyname;
            delete[] copyver;
            bool success = false;
            if (InitResult == EOS_EResult::EOS_Success) {
                EOS_Logging_SetLogLevel(EOS_ELogCategory::EOS_LC_ALL_CATEGORIES, EOS_ELogLevel::EOS_LOG_VeryVerbose);
                EOS_Logging_SetCallback(OnLogMessageReceived);
                env->CallVoidMethod(consumerg, consumerid, nullptr);
                checkException(env);
                success = true;
            } else {
                jstring str = env->NewStringUTF(EOS_EResult_ToString(InitResult));
                env->CallVoidMethod(consumerg, consumerid, str);
                checkException(env);
                env->DeleteLocalRef(str);
            }
            env->DeleteGlobalRef(consumerg);

            {
                std::unique_lock lock(initMutex);
                condIsPlatformArgPresent.wait(lock, []{return platformArgPointer != nullptr;});
            }

            jclass clazz = env->GetObjectClass(platformArgPointer->callback);
            jmethodID consumerPlatformid = env->GetMethodID(clazz, "accept", "(Ljava/lang/Object;)V");
            env->DeleteLocalRef(clazz);

            if (!success) {
                jobject retv = BoxInt(env, -1);
                env->CallVoidMethod(platformArgPointer->callback, consumerPlatformid, retv);
                checkException(env);
                env->DeleteLocalRef(retv);
                env->DeleteGlobalRef(platformArgPointer->callback);
                delete platformArgPointer;
                platformArgPointer = nullptr;
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
            delete[] platformArgPointer->productID;
            delete[] platformArgPointer->clientcredid;
            delete[] platformArgPointer->clientsecret;
            delete[] platformArgPointer->sandboxid;
            delete[] platformArgPointer->deploymentid;
            delete platformArgPointer;
            platformArgPointer = nullptr;
            if (ret != 0) return;

            while (!isShutdown.load()) {
                if (platformHandle != nullptr) {
                    std::lock_guard lock(p2pOptMutex);
                    EOS_Platform_Tick(platformHandle);
                }
                auto localTask = EOSQueue.PopAll();
                while (!localTask.empty() && !isShutdown.load()) {
                    localTask.front()();
                    localTask.pop();
                }
                const auto start = std::chrono::steady_clock::now();
                while (!isShutdown.load()) {
                    // if (!tryReceive(env)) break;
                    if (!tryReceiveFromEOS()) break;

                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > 12) break;
                };
                std::this_thread::yield();
            }
        });

        mainLoopThread->detach();

        ikcpthread = new std::thread([]() {
            ScopedEnv envs;
            JNIEnv* env = envs;
            ScopedFrame frame(env, 256);
            while (!isShutdown.load()) {
                auto now = std::chrono::steady_clock::now().time_since_epoch();
                auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
                {
                    std::shared_lock lock(lock_ikcp);
                    for (const auto& [fp, ikcp]: IKCPs) {
                        IKCPData* data = std::launder(static_cast<IKCPData*>(ikcp->user));
                        auto mtx_lck = data->mutex;
                        std::lock_guard mlock(*mtx_lck);
                        ikcp_update(ikcp, millis);

                        jobject localConsumer;
                        jmethodID localMethodID;
                        {
                            std::lock_guard lockr(receiveMutex);
                            if (globalReceiveConsumer == nullptr) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                                continue;
                            };
                            localConsumer = env->NewLocalRef(globalReceiveConsumer);
                            localMethodID = globalReceiveMethod;
                        }
                        jstring remotePUID = env->NewStringUTF(data->remotePUID);
                        jstring socketID = env->NewStringUTF(data->socketID);
                        while (!isShutdown.load()) {
                            int wsize = ikcp_peeksize(ikcp);
                            if (wsize < 0) break;
                            buffer.reserveIfSmaller(wsize);
                            int size = ikcp_recv(ikcp, buffer, wsize);
                            if (size > 0) {
                                jbyteArray arr = env->NewByteArray(size);
                                env->SetByteArrayRegion(arr, 0, size, std::launder(buffer.getBuf()));
                                env->CallVoidMethod(localConsumer, localMethodID, remotePUID, socketID, data->channel, arr);
                                checkException(env);
                                env->DeleteLocalRef(arr);
                            } else {
                                break;
                            }
                        }
                        env->DeleteLocalRef(remotePUID);
                        env->DeleteLocalRef(socketID);
                        env->DeleteLocalRef(localConsumer);
                    }
                }
                std::this_thread::yield();
            }
        });
        ikcpthread->detach();
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_SetNameGetter(JNIEnv* env, jclass, jobject supplier) {
        if (isShutdown.load()) return;
        ScopedFrame frame(env, 256);
        std::lock_guard lock(supplierMutex);
        if (globalNameSupplier != nullptr) {
            env->DeleteGlobalRef(globalNameSupplier);
        }

        globalNameSupplier = env->NewGlobalRef(supplier);
        jclass clz = env->GetObjectClass(supplier);
        globalSupplierMethod = env->GetMethodID(clz, "get", "()Ljava/lang/Object;");
        env->DeleteLocalRef(clz);
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_SetLogging(JNIEnv* env, jclass, jobject consumer) {
        if (isShutdown.load()) return;
        setGenericCallback<callbackMutex, globalLoggingConsumer, globalBiConsumerMethod, "accept", 2>(env, consumer);
    }

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_getPUID(JNIEnv* env, jclass, jstring uniqueID, jobject callback) {
        if (isShutdown.load()) return;
        ScopedFrame frame(env, 256);
        jclass clz = env->GetObjectClass(callback);
        jmethodID callbackmethod = env->GetMethodID(clz, "accept", "(Ljava/lang/Object;)V");
        env->DeleteLocalRef(clz);

        jobject globalCallback = env->NewGlobalRef(callback);

        EOS_Connect_CreateDeviceIdOptions options = {};
        options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;

        const char* tempChars = env->GetStringUTFChars(uniqueID, nullptr);
        size_t length = strlen(tempChars);
        if (length > EOS_CONNECT_CREATEDEVICEID_DEVICEMODEL_MAX_LENGTH) {
            length = EOS_CONNECT_CREATEDEVICEID_DEVICEMODEL_MAX_LENGTH;
        }
        length++;
        char* copyChars = new char[sizeof(char) * length];
        snprintf(copyChars, length, "%s", tempChars);
        env->ReleaseStringUTFChars(uniqueID, tempChars);

        options.DeviceModel = copyChars;

        ClientData* client_data = new ClientData();
        client_data->copyChars = copyChars;
        client_data->globalCallback = globalCallback;
        client_data->methodID = callbackmethod;

        EOS_Connect_CreateDeviceId(connectHandle, &options, client_data, [](const EOS_Connect_CreateDeviceIdCallbackInfo* data) {
            ScopedEnv envs;
            JNIEnv* env = envs;
            ClientData* client_data = std::launder(static_cast<ClientData*>(data->ClientData));
            if (data->ResultCode == EOS_EResult::EOS_Success || data->ResultCode == EOS_EResult::EOS_DuplicateNotAllowed) {
                Login(client_data, [](const EOS_Connect_LoginCallbackInfo* data) {
                    if (isShutdown.load()) return;
                    ScopedEnv envs;
                    JNIEnv* env = envs;
                    ClientData* client_data = std::launder(static_cast<ClientData*>(data->ClientData));
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
                        delete[] client_data->copyChars;
                        if (client_data->nameIfPresent != nullptr) delete[] client_data->nameIfPresent;
                        delete client_data;
                        return;
                    }
                    if (data->ResultCode == EOS_EResult::EOS_InvalidUser) {
                        EOS_Connect_CreateUserOptions CreateUserOptions = {};
                        CreateUserOptions.ApiVersion = EOS_CONNECT_CREATEUSER_API_LATEST;
                        CreateUserOptions.ContinuanceToken = data->ContinuanceToken;
                        EOS_Connect_CreateUser(connectHandle, &CreateUserOptions, client_data, [](const EOS_Connect_CreateUserCallbackInfo* data) {
                            if (isShutdown.load()) return;
                            ScopedEnv envs;
                            JNIEnv* env = envs;
                            ClientData* client_data = std::launder(static_cast<ClientData*>(data->ClientData));

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
                                    std::string str = fmtns::format("Exception Occur when trying to create new userdata: {}", EOS_EResult_ToString(result));
                                    Log(299, str.c_str());
                                    env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
                                    checkException(env);
                                }
                                env->DeleteGlobalRef(client_data->globalCallback);
                                delete[] client_data->copyChars;
                                if (client_data->nameIfPresent != nullptr) delete[] client_data->nameIfPresent;
                                delete client_data;
                                return;
                            }
                            if (EOS_EResult_IsOperationComplete(data->ResultCode) == EOS_FALSE) {
                                return;
                            }
                            env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
                            checkException(env);
                            env->DeleteGlobalRef(client_data->globalCallback);
                            delete[] client_data->copyChars;
                            if (client_data->nameIfPresent != nullptr) delete[] client_data->nameIfPresent;
                            delete client_data;
                        });
                        return;
                    }
                    if (EOS_EResult_IsOperationComplete(data->ResultCode) == EOS_FALSE) {
                        return;
                    }
                    env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
                    checkException(env);
                    env->DeleteGlobalRef(client_data->globalCallback);
                    delete[] client_data->copyChars;
                    if (client_data->nameIfPresent != nullptr) delete[] client_data->nameIfPresent;
                    delete client_data;
                });
                return;
            }
            if (EOS_EResult_IsOperationComplete(data->ResultCode) == EOS_FALSE) {
                return;
            }
            std::string str = fmtns::format("Exception Occur when trying to get userdata: {}", EOS_EResult_ToString(data->ResultCode));
            Log(299, str.c_str());
            env->CallVoidMethod(client_data->globalCallback, client_data->methodID, nullptr);
            checkException(env);
            env->DeleteGlobalRef(client_data->globalCallback);
            delete[] client_data->copyChars;
            if (client_data->nameIfPresent != nullptr) delete[] client_data->nameIfPresent;
            delete client_data;
        });
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_initConnectionHandle(JNIEnv* env, jclass, jdouble timeout, jstring pid, jstring credid, jstring secret, jstring sandbox, jstring depid, jobject callback) {
        if (isShutdown.load()) return;
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

        env->ReleaseStringUTFChars(pid, pidc);
        env->ReleaseStringUTFChars(credid, credidc);
        env->ReleaseStringUTFChars(secret, secretc);
        env->ReleaseStringUTFChars(sandbox, sandboxc);
        env->ReleaseStringUTFChars(depid, depc);

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
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_setNetworkStatus(JNIEnv*, jclass, jint status) {
        if (isShutdown.load()) return;
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

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeIncomingConnectionRequestHandler(JNIEnv* env, jclass, jobject triconsumer) {
        if (isShutdown.load()) return;
        setGenericCallback<incomingInfoMutex, globalIncomingInfoConsumer, globalIncomingInfoMethod, "accept", 3>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeIncomingConnectionRequest(JNIEnv* env, jclass, jstring puidj, jstring socketj) {
        if (isShutdown.load()) return false;
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionRequest,
            Callback<incomingInfoMutex, globalIncomingInfoConsumer, globalIncomingInfoMethod>::defaultMethod,
            ConnectionRequestNotificationId
        );
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeEstablishedConnectionRequestHandler(JNIEnv* env, jclass, jobject triconsumer) {
        if (isShutdown.load()) return;
        setGenericCallback<establishedInfoMutex, globalEstablishedInfoConsumer, globalEstablishedInfoMethod, "accept", 3>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeEstablishedConnectionRequest(JNIEnv* env, jclass, jstring puidj, jstring socketj) {
        if (isShutdown.load()) return false;
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionEstablished,
            Callback<establishedInfoMutex, globalEstablishedInfoConsumer, globalEstablishedInfoMethod>::defaultMethod,
            ConnectionEstablishedNotificationId
        );
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeInterruptConnectionRequestHandler(JNIEnv* env, jclass, jobject triconsumer) {
        if (isShutdown.load()) return;
        setGenericCallback<interruptInfoMutex, globalInterruptInfoConsumer, globalInterruptInfoMethod, "accept", 3>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeInterruptConnectionRequest(JNIEnv* env, jclass, jstring puidj, jstring socketj) {
        if (isShutdown.load()) return false;
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONINTERRUPTED_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionInterrupted,
            Callback<interruptInfoMutex, globalInterruptInfoConsumer, globalInterruptInfoMethod>::defaultMethod,
            ConnectionInterruptedNotificationId
        );
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeCloseConnectionRequestHandler(JNIEnv* env, jclass, jobject triconsumer) {
        if (isShutdown.load()) return;
        setGenericCallback<closeInfoMutex, globalCloseInfoConsumer, globalCloseInfoMethod, "accept", 4>(env, triconsumer);
    };

    JNIEXPORT jboolean JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_subscribeCloseConnectionRequest(JNIEnv* env, jclass, jstring puidj, jstring socketj) {
        if (isShutdown.load()) return false;
        return SubscribeP2PGenericRequest(
            env, puidj, socketj, EOS_P2P_ADDNOTIFYPEERCONNECTIONCLOSED_API_LATEST,
            EOS_P2P_AddNotifyPeerConnectionClosed,
            Callback<closeInfoMutex, globalCloseInfoConsumer, globalCloseInfoMethod>::closeMethod,
            ConnectionClosedNotificationId
        );
    };

    JNIEXPORT jstring JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_connectOrAccept(JNIEnv* env, jclass, jstring localPUIDj, jstring remotePUIDj, jstring SocketIDj) {
        if (isShutdown.load()) return env->NewStringUTF("--shutdown");
        return doConnectionAction(env, localPUIDj, remotePUIDj, SocketIDj, EOS_P2P_ACCEPTCONNECTION_API_LATEST, +[](EOS_HP2P Handle, const EOS_P2P_AcceptConnectionOptions *Options) {
            char localUserID[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
            char remoteUserID[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
            int32_t size = sizeof(localUserID);
            EOS_ProductUserId_ToString(Options->LocalUserId, localUserID, &size);
            size = sizeof(remoteUserID);
            EOS_ProductUserId_ToString(Options->RemoteUserId, remoteUserID, &size);
            CreateIKCPFor(localUserID, remoteUserID, std::launder(const_cast<char*>(Options->SocketId->SocketName)), 0);
            EOS_EResult result = EOS_P2P_AcceptConnection(Handle, Options);

            if (result != EOS_EResult::EOS_Success) {
                RemoveIKCPFor(localUserID, remoteUserID, std::launder(const_cast<char*>(Options->SocketId->SocketName)), 0);
            }
            return result;
        });
    };

    JNIEXPORT jstring JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_close(JNIEnv* env, jclass, jstring localPUIDj, jstring remotePUIDj, jstring SocketIDj) {
        if (isShutdown.load()) return env->NewStringUTF("--shutdown");
        return doConnectionAction(env, localPUIDj, remotePUIDj, SocketIDj, EOS_P2P_CLOSECONNECTION_API_LATEST, +[](EOS_HP2P Handle, const EOS_P2P_CloseConnectionOptions* Options) {
            EOS_EResult result = EOS_P2P_CloseConnection(Handle, Options);
            if (result == EOS_EResult::EOS_Success) {
                char localUserID[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
                char remoteUserID[EOS_PRODUCTUSERID_MAX_LENGTH + 1];
                int32_t size = sizeof(localUserID);
                EOS_ProductUserId_ToString(Options->LocalUserId, localUserID, &size);
                size = sizeof(remoteUserID);
                EOS_ProductUserId_ToString(Options->RemoteUserId, remoteUserID, &size);
                RemoveIKCPFor(localUserID, remoteUserID, std::launder(const_cast<char*>(Options->SocketId->SocketName)), 0);
            }
            return result;
        });
    };

    JNIEXPORT jstring JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_send(JNIEnv* env, jclass, jstring localPUIDj, jstring remotePUIDj, jstring socketIDj, jbyte channel, jbyteArray dataj) {
        if (isShutdown.load()) return env->NewStringUTF("--shutdown");
        jsize sizep = env->GetArrayLength(dataj);
        if (sizep == 0) return nullptr;

        std::unique_ptr<jbyte[]> bytes(new jbyte[sizep]);
        const uint32_t len = static_cast<uint32_t>(sizep) * sizeof(jbyte);
        env->GetByteArrayRegion(dataj, 0, sizep, bytes.get());

        char* localPUIDs = makeCharFromJString(env, localPUIDj);
        char* remotePUIDs = makeCharFromJString(env, remotePUIDj);
        char* socketIDs = socketIDj == nullptr ? nullptr : makeCharFromJString(env, socketIDj);

        EOS_ProductUserId localPUID = EOS_ProductUserId_FromString(localPUIDs);
        EOS_ProductUserId remotePUID = EOS_ProductUserId_FromString(remotePUIDs);

        if (EOS_ProductUserId_IsValid(remotePUID) == EOS_FALSE) {
            if (socketIDs != nullptr) {
                delete[] socketIDs;
            }
            delete[] localPUIDs;
            delete[] remotePUIDs;
            return env->NewStringUTF("--remote_not_valid");
        }

        if (EOS_ProductUserId_IsValid(localPUID) == EOS_FALSE) {
            if (socketIDs != nullptr) {
                delete[] socketIDs;
            }
            delete[] localPUIDs;
            delete[] remotePUIDs;
            return env->NewStringUTF("--local_not_valid");
        }

        uint64_t hash1 = CalculateHash1(localPUIDs, remotePUIDs, socketIDs, channel);
        uint64_t hash2 = CalculateHash2(localPUIDs, remotePUIDs, socketIDs, channel);

        if (socketIDs != nullptr) {
            delete[] socketIDs;
        }
        delete[] localPUIDs;
        delete[] remotePUIDs;

        Fingerprint fprint = { hash1, hash2 };
        {
            std::shared_lock lock(lock_ikcp);
            auto it = IKCPs.find(fprint);
            if (it == IKCPs.end()) {
                return env->NewStringUTF("--connection_corrupted_unexpected");
            }
            ikcpcb* kcp = it->second;
            auto mtx_ptr = std::launder(static_cast<IKCPData*>(kcp->user))->mutex;
            int sent_val;
            {
                std::lock_guard mlock(*mtx_ptr);
                sent_val = ikcp_send(kcp, std::launder(reinterpret_cast<const char*>(bytes.get())), len);
            }
            if (sent_val < 0) {
                switch (sent_val) {
                    case -1:
                        return env->NewStringUTF("--len_less_than_zero");
                    case -2:
                        return env->NewStringUTF("--seg_failed");
                    default:
                        return env->NewStringUTF("--sent_unknown_error");
                }
            }
            if (sent_val < len) {
                return env->NewStringUTF(std::to_string(sent_val).c_str());
            }
        }
        return nullptr;
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_registerReceiveCallbackFor(JNIEnv* env, jclass, jstring localPUIDj, jobject consumer) {
        if (isShutdown.load()) return;
        std::lock_guard lock(receiveMutex);
        if (globalReceiveConsumer != nullptr) {
            env->DeleteGlobalRef(globalReceiveConsumer);
        }
        if (globalReceivePUID != nullptr) delete[] globalReceivePUID;

        globalReceiveConsumer = env->NewGlobalRef(consumer);
        jclass clz = env->GetObjectClass(consumer);
        globalReceiveMethod = env->GetMethodID(clz, "accept", "(Ljava/lang/String;Ljava/lang/String;B[B)V");
        globalReceivePUID = makeCharFromJString(env, localPUIDj);
        env->DeleteLocalRef(clz);
    };

    JNIEXPORT void JNICALL Java_io_szktas_eos_EOSBinder_EOSNative_teardown(JNIEnv *, jclass) {
        EOSQueue.Run([]() {
            Teardown();
            return nullptr;
        });
    };

    JNIEXPORT void JNICALL JNI_OnUnload(JavaVM*, void*) {
        Teardown();
    }
}