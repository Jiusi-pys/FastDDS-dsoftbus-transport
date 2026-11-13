#include "org_fastdds_standards_b5_NativeBridge.h"
#include "b4.h"
#include <cstring>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
enum class State { New, Open, Closing, Closed };
struct Owner {
    std::mutex mutex; b4_endpoint* endpoint=nullptr; State state=State::New;
#ifdef B5_FAKE_BACKEND
    bool throw_on_close=false,thrown=false;
#endif
};
std::mutex registry_mutex;
std::unordered_map<jlong,std::shared_ptr<Owner>> registry;
uint64_t next_id=1;

void exception(JNIEnv* env,const char* name,const char* message) noexcept {
    if(env->ExceptionCheck())return;
    auto type=env->FindClass(name);
    if(type)env->ThrowNew(type,message);
}
template<class T,class F>T boundary(JNIEnv* env,T failure,F call) noexcept {
    try {return call();}
    catch(const std::bad_alloc&) {exception(env,"java/lang/OutOfMemoryError","Native allocation failed");}
    catch(const std::exception& error) {std::fprintf(stderr,"B5_NATIVE_EXCEPTION %s\n",error.what());exception(env,"java/lang/IllegalStateException","Native operation threw");}
    catch(...) {exception(env,"java/lang/IllegalStateException","Native operation threw");}
    return failure;
}
std::shared_ptr<Owner> lookup(jlong id) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    auto found=registry.find(id);return found==registry.end()?nullptr:found->second;
}
int usable(const Owner& owner) {return owner.state==State::Open?B4_OK:owner.state==State::Closing?B4_ERROR:B4_BAD_PARAMETER;}
struct Sample {
    b4_sample* value=nullptr;
    Sample(){if(b4_sample_create(&value)!=B4_OK)throw std::runtime_error("sample allocation failed");}
    ~Sample(){if(b4_sample_destroy(value)!=B4_OK)std::terminate();}
};
bool copy_bytes(JNIEnv* env,jbyteArray input,void* output,jsize size) {
    if(size)env->GetByteArrayRegion(input,0,size,static_cast<jbyte*>(output));
    return !env->ExceptionCheck();
}
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*,void*) {return JNI_VERSION_1_8;}
JNIEXPORT jstring JNICALL Java_org_fastdds_standards_b5_NativeBridge_abi(JNIEnv* env,jclass) {
    return env->NewStringUTF(B5_ABI);
}
JNIEXPORT jstring JNICALL Java_org_fastdds_standards_b5_NativeBridge_backend(JNIEnv* env,jclass) {
#ifdef B5_FAKE_BACKEND
    return env->NewStringUTF("synthetic-close");
#else
    return env->NewStringUTF("host-udp");
#endif
}
JNIEXPORT jlong JNICALL Java_org_fastdds_standards_b5_NativeBridge_allocate(JNIEnv* env,jclass) {
    return boundary<jlong>(env,0,[&] {
        auto owner=std::make_shared<Owner>();std::lock_guard<std::mutex> lock(registry_mutex);
        if(next_id>static_cast<uint64_t>(std::numeric_limits<jlong>::max()))throw std::overflow_error("owner tokens exhausted");
        jlong id=static_cast<jlong>(next_id++);registry.emplace(id,std::move(owner));return id;
    });
}
JNIEXPORT jlong JNICALL Java_org_fastdds_standards_b5_NativeBridge_liveOwners(JNIEnv* env,jclass) {
    return boundary<jlong>(env,-1,[&] {std::lock_guard<std::mutex> lock(registry_mutex);return static_cast<jlong>(registry.size());});
}
JNIEXPORT jint JNICALL Java_org_fastdds_standards_b5_NativeBridge_open(JNIEnv* env,jclass,jlong id,jint domain,jbyteArray topic,jint writer,jint rep) {
    return boundary<jint>(env,B4_ERROR,[&]() -> jint {
        auto owner=lookup(id);if(!owner)return B4_BAD_PARAMETER;
        std::lock_guard<std::mutex> lock(owner->mutex);
        if(owner->state!=State::New||domain<0||domain>232||(writer!=0&&writer!=1)||(rep!=0&&rep!=2)||!topic)return B4_BAD_PARAMETER;
        auto size=env->GetArrayLength(topic);if(size<1||size>255)return B4_BOUNDS;
        std::string name(static_cast<size_t>(size),'\0');if(!copy_bytes(env,topic,name.data(),size))return B4_ERROR;
        if(name.find('\0')!=std::string::npos)return B4_BAD_PARAMETER;
        b4_options options{static_cast<uint32_t>(domain),name.c_str(),writer,rep};
#ifdef B5_FAKE_BACKEND
        owner->throw_on_close=name=="throw-failure";
#endif
        owner->state=State::Closing; // The existing owner also retains any partial failed open.
        int code=b4_open(&options,&owner->endpoint);
        if(code==B4_OK&&owner->endpoint)owner->state=State::Open;
        else if(code==B4_OK)code=B4_ERROR;
        return code;
    });
}
JNIEXPORT jint JNICALL Java_org_fastdds_standards_b5_NativeBridge_close(JNIEnv* env,jclass,jlong id) {
    return boundary<jint>(env,B4_ERROR,[&]() -> jint {
        auto owner=lookup(id);if(!owner)return B4_BAD_PARAMETER;
        std::lock_guard<std::mutex> lock(owner->mutex);
        if(owner->state==State::Closed)return B4_BAD_PARAMETER;
        owner->state=State::Closing;
#ifdef B5_FAKE_BACKEND
        // Inject inside the C++ JNI body: B4's extern-C API promises never to
        // throw, and MSVC /EHsc relies on that promise for lock unwinding.
        if(owner->throw_on_close&&!owner->thrown){owner->thrown=true;throw std::runtime_error("synthetic JNI C++ close exception");}
#endif
        int code=b4_close(owner->endpoint);
        if(code==B4_OK) {
            owner->endpoint=nullptr;owner->state=State::Closed;
            std::lock_guard<std::mutex> registry_lock(registry_mutex);registry.erase(id);
        }
        return code;
    });
}
JNIEXPORT jint JNICALL Java_org_fastdds_standards_b5_NativeBridge_waitMatched(JNIEnv* env,jclass,jlong id,jint ms) {
    return boundary<jint>(env,B4_ERROR,[&]() -> jint {
        auto owner=lookup(id);if(!owner||ms<0||ms>60000)return B4_BAD_PARAMETER;
        std::lock_guard<std::mutex> lock(owner->mutex);int code=usable(*owner);
        return code==B4_OK?b4_wait_match(owner->endpoint,static_cast<uint32_t>(ms)):code;
    });
}
JNIEXPORT jint JNICALL Java_org_fastdds_standards_b5_NativeBridge_write(JNIEnv* env,jclass,jlong id,jlong key,jlong sequence,jbyteArray label,jbyteArray payload) {
    return boundary<jint>(env,B4_ERROR,[&]() -> jint {
        auto owner=lookup(id);if(!owner||!label||!payload)return B4_BAD_PARAMETER;
        if(key<0||static_cast<uint64_t>(key)>UINT32_MAX||sequence<0||static_cast<uint64_t>(sequence)>UINT32_MAX)return B4_BOUNDS;
        auto label_size=env->GetArrayLength(label),payload_size=env->GetArrayLength(payload);
        if(label_size>64||payload_size>256)return B4_BOUNDS;
        b4_record value{};value.id=static_cast<uint32_t>(key);value.sequence_number=static_cast<uint32_t>(sequence);
        value.label_size=static_cast<size_t>(label_size);value.payload_size=static_cast<size_t>(payload_size);
        if(!copy_bytes(env,label,value.label,label_size)||!copy_bytes(env,payload,value.payload,payload_size))return B4_ERROR;
        if(std::memchr(value.label,0,value.label_size))return B4_BAD_PARAMETER;
        std::lock_guard<std::mutex> lock(owner->mutex);int code=usable(*owner);if(code!=B4_OK)return code;
        Sample sample;code=b4_sample_set(sample.value,&value);return code==B4_OK?b4_write(owner->endpoint,sample.value):code;
    });
}
JNIEXPORT jobject JNICALL Java_org_fastdds_standards_b5_NativeBridge_take(JNIEnv* env,jclass,jlong id,jintArray status) {
    return boundary<jobject>(env,nullptr,[&]() -> jobject {
        if(!status||env->GetArrayLength(status)!=1){exception(env,"java/lang/IllegalArgumentException","Status carrier must have length 1");return nullptr;}
        auto owner=lookup(id);jint code=owner?B4_ERROR:B4_BAD_PARAMETER;
        b4_record value{};
        if(owner) {
            std::lock_guard<std::mutex> lock(owner->mutex);code=usable(*owner);
            if(code==B4_OK){Sample sample;code=b4_take(owner->endpoint,sample.value);if(code==B4_OK)code=b4_sample_get(sample.value,&value);}
        }
        env->SetIntArrayRegion(status,0,1,&code);if(env->ExceptionCheck()||code!=B4_OK)return nullptr;
        auto type=env->FindClass("org/fastdds/standards/b5/BoundedRecord");if(!type)return nullptr;
        auto constructor=env->GetMethodID(type,"<init>","(JJ[B[B)V");if(!constructor)return nullptr;
        auto label=env->NewByteArray(static_cast<jsize>(value.label_size));if(!label){exception(env,"java/lang/OutOfMemoryError","Label allocation failed");return nullptr;}
        auto payload=env->NewByteArray(static_cast<jsize>(value.payload_size));if(!payload){exception(env,"java/lang/OutOfMemoryError","Payload allocation failed");return nullptr;}
        if(value.label_size)env->SetByteArrayRegion(label,0,static_cast<jsize>(value.label_size),reinterpret_cast<const jbyte*>(value.label));
        if(env->ExceptionCheck())return nullptr;
        if(value.payload_size)env->SetByteArrayRegion(payload,0,static_cast<jsize>(value.payload_size),reinterpret_cast<const jbyte*>(value.payload));
        if(env->ExceptionCheck())return nullptr;
        return env->NewObject(type,constructor,static_cast<jlong>(value.id),static_cast<jlong>(value.sequence_number),label,payload);
    });
}
JNIEXPORT jint JNICALL Java_org_fastdds_standards_b5_NativeBridge_waitAcknowledged(JNIEnv* env,jclass,jlong id,jint ms) {
    return boundary<jint>(env,B4_ERROR,[&]() -> jint {
        auto owner=lookup(id);if(!owner||ms<0||ms>60000)return B4_BAD_PARAMETER;
        std::lock_guard<std::mutex> lock(owner->mutex);int code=usable(*owner);
        return code==B4_OK?b4_wait_ack(owner->endpoint,static_cast<uint32_t>(ms)):code;
    });
}
