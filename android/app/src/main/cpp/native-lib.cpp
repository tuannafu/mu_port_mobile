#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_com_main52_android_MainActivity_stringFromJNI(JNIEnv* env, jobject /* this */) {
    const std::string message =
        "Main5.2 Android scaffold ready. Next step: extract a portable core and replace Win32 bootstrap with SDL2.";
    return env->NewStringUTF(message.c_str());
}
