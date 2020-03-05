// todo: better name for dll, upload to repo

// Compile on Linux:
// export JAVA_HOME = <jdk foler>
// g++ -I ${JAVA_HOME}/include -I${JAVA_HOME}/include/linux/ -g3 -shared -fPIC agent.cpp -static-libstdc++ -o agent.so

// Run: java -agentpath:<dll/so file>=<folder to create logs> ...

// Some dev notes:
// - in general, when developing, you should run with the vm flag with -Xcheck:jni, to get more debug messages for possible errors
// - be careful with calling back to java code in the events, as this can cause all kind of unexpected issues or recursions
// - don't initialize method static variables with jvm calls from within the callbacks, because their initialization is
//   synchronized and this might causes deadlocks or exceptions

#include "shared.h"

static FILE* output_file_cls_load = NULL;
static FILE* output_file_exceptions = NULL;
static std::mutex mtx_cls_load;
static std::mutex mtx_exceptions;

// Helper macros to simplify error handling

#define _STR_IMPL(x) #x
#define STR(x) _STR_IMPL(x)

#define THROW_ON_ERROR(cond, msg) \
if (cond) { throw std::runtime_error(msg " at " __FILE__ ":" STR(__LINE__)); }

#define THROW_ON_JNI_ERROR(cond, msg) \
if (cond) \
{ \
	jni_env->ExceptionClear(); /* not much else to do. Calling ExceptionDescribe will callback to java code which can cause various issues */ \
	throw std::runtime_error(msg " at " __FILE__ ":" STR(__LINE__)); \
}

#define THROW_ON_JVMTI_ERROR(ret, msg) \
if (ret != JVMTI_ERROR_NONE) \
{ \
	const char *_fullmsg = msg " at " __FILE__ ":" STR(__LINE__); \
	char *_err = new char[strlen(_fullmsg)+10]; \
	sprintf(_err, "%s (%d)", _fullmsg, ret); \
	std::runtime_error _ex(_err); \
	delete[] _err; \
	throw _ex; \
}

#define JNI_FIND_CLASS(var_name, cls) { var_name = jni_env->FindClass(cls); THROW_ON_JNI_ERROR(var_name == NULL, "FindClass " cls); }
#define JNI_FIND_METHOD_ID(var_name, cls, method_name, method_sig) { var_name = jni_env->GetMethodID(cls, method_name, method_sig); THROW_ON_JNI_ERROR(var_name == NULL, "GetMethodID " method_name method_sig); }

class Util
{
public:
	jlong static GetJavaThreadID(JNIEnv* jni_env, jthread thread)
	{
		jclass thread_cls = jni_env->FindClass("java/lang/Thread");
		THROW_ON_JNI_ERROR(thread_cls == NULL, "GetJavaThreadID java/lang/Thread");
		jmethodID thread_getId_method = jni_env->GetMethodID(thread_cls, "getId", "()J");
		THROW_ON_JNI_ERROR(thread_getId_method == NULL, "GetMethodID getId");

		jlong tid = jni_env->CallLongMethod(thread, thread_getId_method);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "GetMethodID getId");
		return tid;
	}

	char static *GetMethodString(jvmtiEnv* jvmti_env, jmethodID method)
	{
		jvmtiError jvmti_error;

		jclass cls = NULL;
		char* cls_sig = NULL, * cls_generic_sig = NULL, * meth_name = NULL, * meth_sig = NULL, * meth_generic_sig = NULL;
		jvmti_error = jvmti_env->GetMethodDeclaringClass(method, &cls);
		THROW_ON_JVMTI_ERROR(jvmti_error, "GetMethodString GetMethodDeclaringClass");
		jvmti_error = jvmti_env->GetClassSignature(cls, &cls_sig, &cls_generic_sig);
		THROW_ON_JVMTI_ERROR(jvmti_error, "GetMethodString GetClassSignature");
		jvmti_error = jvmti_env->GetMethodName(method, &meth_name, &meth_sig, &meth_generic_sig);
		THROW_ON_JVMTI_ERROR(jvmti_error, "GetMethodString GetMethodName");

		size_t ret_len = sizeof("#" " : ") + strlen(cls_sig) + strlen(meth_name) + strlen(meth_sig);
		char* ret = new char[ret_len];
		sprintf(ret, "%s#%s : %s", cls_sig, meth_name, meth_sig);

		return ret;
	}

	wchar_t static* GetCStr(JNIEnv* jni_env, jstring jstr)
	{
		std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;

		jsize str_len = jni_env->GetStringUTFLength(jstr);
		const char* utf_str = jni_env->GetStringUTFChars(jstr, NULL);
		THROW_ON_ERROR(utf_str == NULL, "GetCStr GetStringUTFChars");
		std::wstring wstr = converter.from_bytes(utf_str);
		// Release the memory pinned char array
		jni_env->ReleaseStringUTFChars(jstr, utf_str);

		wchar_t* ret = wcsdup(wstr.c_str());

		return ret;
	}


	wchar_t static* GetExceptionStackTrace(JNIEnv* jni_env, jobject throwable)
	{
		// Find all methods
		jclass sw_class; JNI_FIND_CLASS(sw_class, "java/io/StringWriter");
		jclass pw_class; JNI_FIND_CLASS(pw_class, "java/io/PrintWriter");
		jclass str_buf_class; JNI_FIND_CLASS(str_buf_class, "java/lang/StringBuffer");

		jmethodID sw_ctor; JNI_FIND_METHOD_ID(sw_ctor, sw_class, "<init>", "()V");
		jmethodID pw_ctor; JNI_FIND_METHOD_ID(pw_ctor, pw_class, "<init>", "(Ljava/io/Writer;)V");
		jmethodID sw_getbuf; JNI_FIND_METHOD_ID(sw_getbuf, sw_class, "getBuffer", "()Ljava/lang/StringBuffer;");
		jmethodID str_buf_set_len; JNI_FIND_METHOD_ID(str_buf_set_len, str_buf_class, "setLength", "(I)V");

		jclass throwable_cls; JNI_FIND_CLASS(throwable_cls, "java/lang/Throwable");
		jmethodID throwable_print_stack; JNI_FIND_METHOD_ID(throwable_print_stack, throwable_cls, "printStackTrace", "(Ljava/io/PrintWriter;)V");

		jclass object_cls; JNI_FIND_CLASS(object_cls, "java/lang/Object");
		jmethodID object_to_string; JNI_FIND_METHOD_ID(object_to_string, object_cls, "toString", "()Ljava/lang/String;");

		// Create objects
		jobject str_writer = jni_env->NewObject(sw_class, sw_ctor);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "NewObject sw_ctor");
		jobject print_writer = jni_env->NewObject(pw_class, pw_ctor, str_writer);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "NewObject pw_ctor");

		// Call printStackTrace
		jni_env->CallVoidMethod(throwable, throwable_print_stack, print_writer);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "CallVoidMethod throwable_print_stack");

		// Call StringWriter.toString()
		jstring printStackTrace_str_result = (jstring)jni_env->CallObjectMethod(str_writer, object_to_string);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "CallObjectMethod object_to_string");
		wchar_t* printStackTrace_cstr_result = Util::GetCStr(jni_env, printStackTrace_str_result);

		// Call StringWriter.getBuffer().setLength(0) to reset
		jobject buf = jni_env->CallObjectMethod(str_writer, sw_getbuf);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "CallObjectMethod sw_getbuf");
		jni_env->CallVoidMethod(buf, str_buf_set_len, 0);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "CallVoidMethod str_buf_set_len");

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
	try
	{
		jvmtiError jvmti_error;
		
		jclass object_cls; JNI_FIND_CLASS(object_cls, "java/lang/Object");
		jmethodID object_to_string; JNI_FIND_METHOD_ID(object_to_string, object_cls, "toString", "()Ljava/lang/String;");

		jlong tid = Util::GetJavaThreadID(jni_env, thread);

		// Get exception.toString()
		jstring ex_to_str_result = (jstring)jni_env->CallObjectMethod(exception, object_to_string);
		THROW_ON_JNI_ERROR(jni_env->ExceptionCheck() == JNI_TRUE, "CallObjectMethod object_to_string");
		const wchar_t* ex_to_cstr_result = Util::GetCStr(jni_env, ex_to_str_result);

		const wchar_t* ex_print_stack_result = Util::GetExceptionStackTrace(jni_env, exception);

		jint ex_hash;
		jvmti_error = jvmti_env->GetObjectHashCode(exception, &ex_hash);
		THROW_ON_JVMTI_ERROR(jvmti_error, "CallObjectMethod object_to_string");

		// Print catch location
		std::lock_guard<std::mutex> lock(mtx_exceptions);
		if (catch_method != NULL)
		{
			const char* meth_str = Util::GetMethodString(jvmti_env, catch_method);
			fprintf(output_file_exceptions, PRINT_PREFIX "callback_on_Exception - %" PRId64 " - %ls - 0x%" PRIX32 "\n\t- will be caught in: %s %" PRId64 "\n%ls\n", tid, ex_to_cstr_result, ex_hash, meth_str, catch_location, ex_print_stack_result);
			delete[] meth_str;
		}
		else
		{
			fprintf(output_file_exceptions, PRINT_PREFIX "callback_on_Exception - %" PRId64 " - %ls - 0x%" PRIX32 "\n\t- will not be caught!!\n%ls\n", tid, ex_to_cstr_result, ex_hash, ex_print_stack_result);
		}
		delete[] ex_to_cstr_result;
		delete[] ex_print_stack_result;
	}
	catch (std::exception & ex)
	{
		printf(PRINT_PREFIX "Error: callback_on_Exception: %s\n", ex.what());
	}
}


void JNICALL callback_on_ClassLoad(
	jvmtiEnv* jvmti_env,
	JNIEnv* jni_env,
	jthread thread,
	jclass klass)
{
	try
	{
		jvmtiError jvmti_error;

		jlong tid = Util::GetJavaThreadID(jni_env, thread);
		char* cls_sig = NULL, * cls_generic_sig = NULL;

		jvmti_error = jvmti_env->GetClassSignature(klass, &cls_sig, &cls_generic_sig);
		THROW_ON_JVMTI_ERROR(jvmti_error, "GetClassSignature klass");

		jvmtiFrameInfo frames[100];
		jint count;
		jvmti_env->GetStackTrace(thread, 0, sizeof(frames) / sizeof(frames[0]), (jvmtiFrameInfo*)&frames, &count);
		THROW_ON_JVMTI_ERROR(jvmti_error, "GetStackTrace");

		std::lock_guard<std::mutex> lock(mtx_cls_load);
		fprintf(output_file_cls_load, PRINT_PREFIX "callback_on_ClassLoad - % " PRId64 " - %s%s\n", tid, cls_sig, count == 0 ? "(no java stack)" : "");
		for (jint i = 0; i < count; ++i)
		{
			const char* meth_str = Util::GetMethodString(jvmti_env, frames[i].method);
			fprintf(output_file_cls_load, "\tat %s% " PRId64 "\n", meth_str, frames[i].location);
			delete[] meth_str;
		}
		fprintf(output_file_cls_load, "\n");
	}
	catch (std::exception & ex)
	{
		printf(PRINT_PREFIX "Error: callback_on_ClassLoad: %s\n", ex.what());
	}
}

extern "C" JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM* vm, char* options, void* reserved)
{
	try
	{
		jint ret;
		printf(PRINT_PREFIX "Info: Agent_OnLoad - options: %s\n", options == NULL ? "" : options);
		THROW_ON_ERROR(options == NULL, "expecting options to contain folder path");

		// get env
		jvmtiEnv* jvmti = NULL;
		ret = vm->GetEnv((void**)&jvmti, JVMTI_VERSION_1);
		THROW_ON_JVMTI_ERROR(ret, "GetEnv");

		// add capabilities
		jvmtiCapabilities capa = {};
		capa.can_generate_exception_events = 1;
		ret = jvmti->AddCapabilities(&capa);
		THROW_ON_JVMTI_ERROR(ret, "AddCapabilities");

		// set callback functions - currently only VMStart event
		jvmtiEventCallbacks callbacks = {};
		callbacks.Exception = callback_on_Exception;
		callbacks.ClassLoad = callback_on_ClassLoad;
		ret = jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
		THROW_ON_JVMTI_ERROR(ret, "SetEventCallbacks");
		ret = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_EXCEPTION, NULL);
		THROW_ON_JVMTI_ERROR(ret, "SetEventNotificationMode 1");
		ret = jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_LOAD, NULL);
		THROW_ON_JVMTI_ERROR(ret, "SetEventNotificationMode 2");

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
	catch (std::exception & ex)
	{
		printf(PRINT_PREFIX "Error: Agent_OnLoad: %s\n", ex.what());
		return JNI_ERR;
	}
}
