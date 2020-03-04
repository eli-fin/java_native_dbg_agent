// Compile on Linux:
// export JAVA_HOME = <jdk foler>
// g++ -I ${JAVA_HOME}/include -I${JAVA_HOME}/include/linux/ -g3 -shared -fPIC agent.cpp -o agent.so

// It's recommended to run the vm with -Xcheck:jni, to get more debug messages

#include "shared.h"

// todo: print catch file/line, print thread id, print to file

static FILE* output_file_cls_load = NULL;
static FILE* output_file_exceptions = NULL;

class Util
{
public:
	jlong static GetJavaThreadID(JNIEnv* jni_env, jthread thread)
	{
		static jboolean isFirst = JNI_TRUE;
		static jclass thread_cls = NULL;
		static jmethodID thread_getId_method = NULL;
		if (isFirst == JNI_TRUE)
		{
			thread_cls = jni_env->FindClass("java/lang/Thread");
			thread_getId_method = jni_env->GetMethodID(thread_cls, "getId", "()J");
			isFirst = JNI_FALSE;
		}

		jlong tid = jni_env->CallLongMethod(thread, thread_getId_method);
		return tid;
	}

	char static *GetMethodString(jvmtiEnv* jvmti_env, jmethodID method)
	{
		jclass cls = NULL;
		char* cls_sig = NULL, * cls_generic_sig = NULL, * meth_name = NULL, * meth_sig = NULL, * meth_generic_sig = NULL;
		jvmti_env->GetMethodDeclaringClass(method, &cls);
		jvmti_env->GetClassSignature(cls, &cls_sig, &cls_generic_sig);
		jvmti_env->GetMethodName(method, &meth_name, &meth_sig, &meth_generic_sig);

		size_t ret_len = sizeof("#" " : ") + strlen(cls_sig) + strlen(meth_name) + strlen(meth_sig);
		char* ret = new char[ret_len];
		sprintf(ret, "%s#%s : %s", cls_sig, meth_name, meth_sig);

		return ret;
	}

	wchar_t static* GetCStr(JNIEnv* jni_env, jstring jstr)
	{
		static std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

		jsize str_len = jni_env->GetStringUTFLength(jstr);

		const char* utf_str = jni_env->GetStringUTFChars(jstr, NULL);
		std::wstring wstr = converter.from_bytes(utf_str);
		// Release the memory pinned char array
		jni_env->ReleaseStringUTFChars(jstr, utf_str);

		wchar_t* ret = wcsdup(wstr.c_str());

		return ret;
	}

	wchar_t static* GetExceptionStackTrace(JNIEnv* jni_env, jobject throwable)
	{
		// Find all methods
		static jclass sw_class = jni_env->FindClass("Ljava/io/StringWriter;");
		static jclass pw_class = jni_env->FindClass("Ljava/io/PrintWriter;");
		static jclass str_buf_class = jni_env->FindClass("Ljava/lang/StringBuffer;");
		static jmethodID sw_ctor = jni_env->GetMethodID(sw_class, "<init>", "()V");
		static jmethodID pw_ctor = jni_env->GetMethodID(pw_class, "<init>", "(Ljava/io/Writer;)V");
		static jmethodID sw_getbuf = jni_env->GetMethodID(sw_class, "getBuffer", "()Ljava/lang/StringBuffer;");
		static jmethodID str_buf_set_len = jni_env->GetMethodID(str_buf_class, "setLength", "(I)V");

		static jclass throwable_cls = jni_env->FindClass("Ljava/lang/Throwable;");
		static jmethodID throwable_print_stack = jni_env->GetMethodID(throwable_cls, "printStackTrace", "(Ljava/io/PrintWriter;)V");

		static jclass object_cls = jni_env->FindClass("Ljava/lang/Object;");
		static jmethodID object_to_string = jni_env->GetMethodID(object_cls, "toString", "()Ljava/lang/String;");

		// Create objects
		static jobject str_writer = jni_env->NewObject(sw_class, sw_ctor);
		static jobject print_writer = jni_env->NewObject(pw_class, pw_ctor, str_writer);

		// Call printStackTrace
		jni_env->CallVoidMethod(throwable, throwable_print_stack, print_writer);

		// Call StringWriter.toString()
		jstring printStackTrace_str_result = (jstring)jni_env->CallObjectMethod(str_writer, object_to_string);
		wchar_t* printStackTrace_cstr_result = Util::GetCStr(jni_env, printStackTrace_str_result);

		// Call StringWriter.getBuffer().setLength(0) to reset
		jobject buf = jni_env->CallObjectMethod(str_writer, sw_getbuf);
		jni_env->CallVoidMethod(buf, str_buf_set_len, 0);

		return printStackTrace_cstr_result;
	}
};

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
	static std::mutex mtx;
	static jclass object_cls = jni_env->FindClass("Ljava/lang/Object;");
	static jmethodID object_to_string = jni_env->GetMethodID(object_cls, "toString", "()Ljava/lang/String;");

	jlong tid = Util::GetJavaThreadID(jni_env, thread);

	// Get exception.toString()
	jstring ex_to_str_result = (jstring)jni_env->CallObjectMethod(exception, object_to_string);
	const wchar_t* ex_to_cstr_result = Util::GetCStr(jni_env, ex_to_str_result);
	
	const wchar_t* ex_print_stack_result = Util::GetExceptionStackTrace(jni_env, exception);

	jint ex_hash;
	jvmti_env->GetObjectHashCode(exception, &ex_hash);
	
	// Print catch location
	std::lock_guard<std::mutex> lock(mtx);
	if (catch_method != NULL)
	{
		const char* meth_str = Util::GetMethodString(jvmti_env, catch_method);
		fprintf(output_file_exceptions, PRINT_PREFIX "callback_on_Exception - %lld - %ls - 0x%" PRIX32 "\n\t- will be caught in: %s %" PRId64 "\n%ls\n", tid, ex_to_cstr_result, ex_hash, meth_str, catch_location, ex_print_stack_result);
		delete[] meth_str;
	}
	else
	{
		fprintf(output_file_exceptions, PRINT_PREFIX "callback_on_Exception - %lld - %ls - 0x%" PRIX32 "\n\t- will not be caught!!\n%ls\n", tid, ex_to_cstr_result, ex_hash, ex_print_stack_result);
	}
	delete[] ex_to_cstr_result;
	delete[] ex_print_stack_result;
}


void JNICALL callback_on_ClassLoad(
	jvmtiEnv* jvmti_env,
	JNIEnv* jni_env,
	jthread thread,
	jclass klass)
{
	static std::mutex mtx;
	jlong tid = Util::GetJavaThreadID(jni_env, thread);
	char* cls_sig = NULL, * cls_generic_sig = NULL;

	jvmti_env->GetClassSignature(klass, &cls_sig, &cls_generic_sig);

	jvmtiFrameInfo frames[100];
	jint count;
	jvmti_env->GetStackTrace(thread, 0, sizeof(frames) / sizeof(frames[0]), (jvmtiFrameInfo*)&frames, &count);

	std::lock_guard<std::mutex> lock(mtx);
	fprintf(output_file_cls_load, PRINT_PREFIX "callback_on_ClassLoad - %lld - %s%s\n", tid, cls_sig, count == 0 ? "(no java stack)" : "");
	for (jint i = 0; i < count; ++i)
	{
		const char* meth_str = Util::GetMethodString(jvmti_env, frames[i].method);
		fprintf(output_file_cls_load, "\tat %s% " PRId64 "\n", meth_str, frames[i].location);
		delete[] meth_str;
	}
	fprintf(output_file_cls_load, "\n");
}

extern "C" JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM* vm, char* options, void* reserved)
{
	jint ret;
	printf(PRINT_PREFIX "Info: Agent_OnLoad - options: %s\n", options == NULL ? "" : options);
	if (options == NULL)
	{
		printf(PRINT_PREFIX "Error: Agent_OnLoad - expecting options to contain folder path\n");
		return JNI_ERR;
	}

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
	callbacks.ClassLoad = callback_on_ClassLoad;
	if ((ret = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks))) != JNI_OK) {
		printf(PRINT_PREFIX "Error: Agent_OnLoad - SetEventCallbacks, ret=0x%08" PRIX32 "\n", ret);
		return JNI_ERR;
	}
	if ((ret = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_EXCEPTION, NULL)) != JNI_OK) {
		printf(PRINT_PREFIX "Error: Agent_OnLoad - SetEventNotificationMode EXCEPTION, ret=0x%08" PRIX32 "\n", ret);
		return JNI_ERR;
	}
	if ((ret = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_LOAD, NULL)) != JNI_OK) {
		printf(PRINT_PREFIX "Error: Agent_OnLoad - SetEventNotificationMode CLASS_LOAD, ret=0x%08" PRIX32 "\n", ret);
		return JNI_ERR;
	}

	// Open log files
	char fname[1000];
	sprintf(fname, "%s/cx_cls_loads_%d.log", options, getpid());
	printf(PRINT_PREFIX "Opening file %s\n", fname);
	if ((output_file_cls_load = fopen(fname, "wb")) == NULL)
	{
		perror(PRINT_PREFIX "Error: Agent_OnLoad - fopen: ");
		return JNI_ERR;
	}
	sprintf(fname, "%s/cx_exceptions_%d.log", options, getpid());
	printf(PRINT_PREFIX "Opening file %s\n", fname);
	if ((output_file_exceptions = fopen(fname, "wb")) == NULL)
	{
		perror(PRINT_PREFIX "Error: Agent_OnLoad - fopen: ");
		return JNI_ERR;
	}

	return JNI_OK;
}
