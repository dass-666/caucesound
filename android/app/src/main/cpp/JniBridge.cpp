// Puente JNI mínimo: los comandos van como JSON a cs::handleCommand (el mismo contrato que usará iOS);
// solo las consultas de alta frecuencia (posición, medidores, formas de onda) tienen funciones propias.
#include "AudioIO.h"

#include "cs/Api.h"
#include "cs/Session.h"

#include <jni.h>

#include <memory>
#include <string>

namespace
{
struct Native
{
    cs::Engine engine;
    cs::Session session { engine };
    csandroid::AudioIO audio { engine };
    Native() { engine.prepare (48000, 1024); }
};
Native& native()
{
    static Native* n = new Native(); // vive durante todo el proceso (el hilo de audio puede seguir activo)
    return *n;
}

std::string str (JNIEnv* env, jstring s)
{
    if (! s) return {};
    const char* c = env->GetStringUTFChars (s, nullptr);
    std::string r (c ? c : "");
    if (c) env->ReleaseStringUTFChars (s, c);
    return r;
}
} // namespace

extern "C"
{
JNIEXPORT jboolean JNICALL Java_com_caucesound_app_audio_NativeEngine_audioStart (JNIEnv*, jobject) { return native().audio.start(); }
JNIEXPORT jboolean JNICALL Java_com_caucesound_app_audio_NativeEngine_audioEnsure (JNIEnv*, jobject) { return native().audio.ensureRunning(); }
JNIEXPORT void JNICALL Java_com_caucesound_app_audio_NativeEngine_audioStop (JNIEnv*, jobject) { native().audio.stop(); }
JNIEXPORT jboolean JNICALL Java_com_caucesound_app_audio_NativeEngine_setInputEnabled (JNIEnv*, jobject, jboolean on) { return native().audio.setInputEnabled (on); }
JNIEXPORT jboolean JNICALL Java_com_caucesound_app_audio_NativeEngine_setEchoCancel (JNIEnv*, jobject, jboolean on) { return native().audio.setEchoCancel (on); }

JNIEXPORT jstring JNICALL Java_com_caucesound_app_audio_NativeEngine_audioStatus (JNIEnv* env, jobject)
{
    return env->NewStringUTF (native().audio.statusJson().c_str());
}

JNIEXPORT jstring JNICALL Java_com_caucesound_app_audio_NativeEngine_command (JNIEnv* env, jobject, jstring json)
{
    Native& n = native();
    const std::string r = cs::handleCommand (n.session, n.engine, str (env, json));
    return env->NewStringUTF (r.c_str());
}

JNIEXPORT jlong JNICALL Java_com_caucesound_app_audio_NativeEngine_position (JNIEnv*, jobject) { return (jlong) native().engine.position(); }
JNIEXPORT jlong JNICALL Java_com_caucesound_app_audio_NativeEngine_countInRemaining (JNIEnv*, jobject) { return (jlong) native().engine.countInRemaining(); }
JNIEXPORT jboolean JNICALL Java_com_caucesound_app_audio_NativeEngine_isPlaying (JNIEnv*, jobject) { return native().engine.isPlaying(); }
JNIEXPORT jlong JNICALL Java_com_caucesound_app_audio_NativeEngine_recordedFrames (JNIEnv*, jobject) { return (jlong) native().session.recordingFrames(); }

JNIEXPORT jboolean JNICALL Java_com_caucesound_app_audio_NativeEngine_liveMidi (JNIEnv*, jobject, jint slot, jint status, jint d1, jint d2)
{
    if (slot < 0 || slot >= cs::kMaxTracks) return false;
    return native().engine.sendLive ((int) slot, (uint8_t) status, (uint8_t) (d1 & 0x7F), (uint8_t) (d2 & 0x7F));
}

// [master, pista0, pista1, ...] picos lineales del último bloque
JNIEXPORT jfloatArray JNICALL Java_com_caucesound_app_audio_NativeEngine_meters (JNIEnv* env, jobject, jint numTracks)
{
    Native& n = native();
    const int count = std::max (0, std::min ((int) numTracks, cs::kMaxTracks));
    jfloatArray arr = env->NewFloatArray (count + 1);
    std::vector<float> v ((size_t) count + 1);
    v[0] = n.engine.masterPeak();
    for (int i = 0; i < count; ++i) v[(size_t) i + 1] = n.engine.trackParams (i).peak.load (std::memory_order_relaxed);
    env->SetFloatArrayRegion (arr, 0, count + 1, v.data());
    return arr;
}

JNIEXPORT jfloatArray JNICALL Java_com_caucesound_app_audio_NativeEngine_peaks (JNIEnv* env, jobject, jstring sourceId, jlong from, jlong to, jint buckets)
{
    const auto p = native().session.peaks (str (env, sourceId), from, to, buckets);
    jfloatArray arr = env->NewFloatArray ((jsize) p.size());
    env->SetFloatArrayRegion (arr, 0, (jsize) p.size(), p.data());
    return arr;
}
}
