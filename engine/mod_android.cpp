#ifdef __ANDROID__
#include <jni.h>
#include "ra2assets/mod_profile.hpp"
extern "C" JNIEXPORT jstring JNICALL Java_org_ra2_engine_ModManager_nativeReport(JNIEnv* env,jclass,jstring path) {
    if(!path)return env->NewStringUTF("BLOCKED\nNo mod folder selected");
    const char* raw=env->GetStringUTFChars(path,nullptr);if(!raw)return nullptr;
    const std::string directory(raw);env->ReleaseStringUTFChars(path,raw);
    try {const auto mod=ra2assets::ModProfile::inspect(directory);return env->NewStringUTF(mod->report().c_str());}
    catch(const std::exception& e){return env->NewStringUTF((std::string("BLOCKED\n")+e.what()).c_str());}
}
#endif
