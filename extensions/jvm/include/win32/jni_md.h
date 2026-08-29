#ifndef SLP_JNI_MD_H
#define SLP_JNI_MD_H

/* JNI's platform header is ABI declarations only.  Keep a Windows copy with
 * the provider so BEMU can cross-compile against a host JDK's portable
 * jni.h without requiring a second, target-host JDK installation. */
#define JNIEXPORT __declspec(dllexport)
#define JNIIMPORT __declspec(dllimport)
#define JNICALL __stdcall

typedef long jint;
typedef __int64 jlong;
typedef signed char jbyte;

#endif
