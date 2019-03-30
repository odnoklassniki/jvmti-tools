/*
 * Copyright 2019 Odnoklassniki Ltd, Mail.Ru Group
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jvmti.h>
#include <string.h>

void JNICALL VMInit(jvmtiEnv* jvmti, JNIEnv* env, jthread thread) {
    jclass Module = env->FindClass("java/lang/Module");
    if (Module == NULL) {
        // Seems like pre-module JDK
        env->ExceptionClear();
        return;
    }

    jmethodID getPackages = env->GetMethodID(Module, "getPackages", "()Ljava/util/Set;");

    jmethodID getUnnamedModule = env->GetMethodID(
        env->FindClass("java/lang/ClassLoader"), "getUnnamedModule", "()Ljava/lang/Module;");

    jmethodID toString = env->GetMethodID(
        env->FindClass("java/lang/Object"), "toString", "()Ljava/lang/String;");

    // Get unnamed module of the current thread's ClassLoader
    jvmtiThreadInfo thread_info;
    jvmti->GetThreadInfo(NULL, &thread_info);

    jobject unnamed_module = env->CallObjectMethod(thread_info.context_class_loader, getUnnamedModule);

    jint module_count = 0;
    jobject* modules = NULL;
    jvmti->GetAllModules(&module_count, &modules);

    // Scan all loaded modules
    for (int i = 0; i < module_count; i++) {
        jvmti->AddModuleReads(modules[i], unnamed_module);

        // Get all module packages as one string: "[java.lang, java.io, ...]"
        jobject packages = env->CallObjectMethod(modules[i], getPackages);
        jstring str = (jstring) env->CallObjectMethod(packages, toString);

        char* c_str = (char*) env->GetStringUTFChars(str, NULL);
        if (c_str == NULL) continue;

        // Export and open every package to the unnamed module
        char* package = strtok(c_str + 1, ", ]");
        while (package != NULL) {
            jvmti->AddModuleExports(modules[i], package, unnamed_module);
            jvmti->AddModuleOpens(modules[i], package, unnamed_module);
            package = strtok(NULL, ", ]");
        }

        env->ReleaseStringUTFChars(str, c_str);
    }

    jvmti->Deallocate((unsigned char*) modules);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
    jvmtiEnv* jvmti;
    vm->GetEnv((void**) &jvmti, JVMTI_VERSION_1_0);

    jvmtiEventCallbacks callbacks = {0};
    callbacks.VMInit = VMInit;
    jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);

    return 0;
}
