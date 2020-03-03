// Compile on Linux:
// export JAVA_HOME = <jdk foler>
// g++ -I ${JAVA_HOME}/include -I${JAVA_HOME}/include/linux/ -shared -fPIC agent.cpp -o agent.so

#include "shared.h"

// todo: print catch file/line, print thread id, print to file

void JNICALL callback_on_Exception(
	jvmtiEnv* jvmti_env,
	JNIEnv* jni_env,
	jthread thread,
	jmethodID method,
	jlocation location,
	jobject exception,
	jmethodID catch_method,
	jlocation catch_location)
{
	jclass ex_cls = jni_env->GetObjectClass(exception);

	// Get exception.toString()
	jmethodID methodId = jni_env->GetMethodID(ex_cls, "toString", "()Ljava/lang/String;");
	jstring methodRet = (jstring)jni_env->CallObjectMethod(ex_cls, methodId);
	jsize str_len = jni_env->GetStringUTFLength(methodRet);
	jboolean str_is_copy = JNI_FALSE;
	const char* str = jni_env->GetStringUTFChars(methodRet, &str_is_copy);
	jint ex_hash;
	jvmti_env->GetObjectHashCode(exception, &ex_hash);
	printf(PRINT_PREFIX "callback_on_Exception - %s - 0x%" PRIX32 "\n\t- ", str, ex_hash);

	// Get catch location
	jclass cls = NULL;
	char * cls_sig = NULL, * cls_generic_sig = NULL, * meth_name = NULL, * meth_sig = NULL, * meth_generic_sig = NULL;
	if (catch_method != NULL)
	{
		jvmti_env->GetMethodDeclaringClass(catch_method, &cls);
		jvmti_env->GetClassSignature(cls, &cls_sig, &cls_generic_sig);
		jvmti_env->GetMethodName(catch_method, &meth_name, &meth_sig, &meth_generic_sig);
		printf(PRINT_PREFIX "will be caught in: %s#%s:%s %" PRId64 "\n", cls_sig, meth_name, meth_sig, catch_location);
	}
	else
	{
		printf(PRINT_PREFIX "will not be caught!!\n");
	}
	
	// Call printStackTrace
	jclass throwable_cls = jni_env->FindClass("Ljava/lang/Throwable;");
	methodId = jni_env->GetMethodID(throwable_cls, "printStackTrace", "()V");
	jni_env->CallVoidMethod(exception, methodId);

	// Release the memory pinned char array
	if (str_is_copy == JNI_TRUE)
		jni_env->ReleaseStringUTFChars(methodRet, str);
}


extern "C" JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM* vm, char* options, void* reserved)
{
	jint ret;
	printf(PRINT_PREFIX "Info: Agent_OnLoad\n");

	// get env
	jvmtiEnv* jvmti = NULL;
	if ((ret = vm->GetEnv((void**)&jvmti, JVMTI_VERSION_1)) != JNI_OK) {
		printf(PRINT_PREFIX "Error: Agent_OnLoad - GetEnv, ret=0x%08" PRIX32 "\n", ret);
		return JNI_ERR;
	}

	// add capabilities
	jvmtiCapabilities capa = {};
	capa.can_generate_exception_events = 1;
	if ((ret = jvmti->AddCapabilities(&capa)) != JNI_OK) {
		printf(PRINT_PREFIX "Error: Agent_OnLoad - AddCapabilities, ret=0x%08" PRIX32 "\n", ret);
		return JNI_ERR;
	}

	// set callback functions - currently only VMStart event
	jvmtiEventCallbacks callbacks = {};
	callbacks.Exception = callback_on_Exception;
	if ((ret = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks))) != JNI_OK) {
		printf(PRINT_PREFIX "Error: Agent_OnLoad - SetEventCallbacks, ret=0x%08" PRIX32 "\n", ret);
		return JNI_ERR;
	}
	if ((ret = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_EXCEPTION, NULL)) != JNI_OK) {
		printf(PRINT_PREFIX "Error: Agent_OnLoad - SetEventNotificationMode, ret=0x%08" PRIX32 "\n", ret);
		return JNI_ERR;
	}

	return JNI_OK;
}
