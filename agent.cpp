#include "shared.h"

extern "C" JNIEXPORT jint JNICALL
Agent_OnLoad(JavaVM* vm, char* options, void* reserved)
{
	printf("Hi, Agent_OnLoad\n");
	return 0;
}
