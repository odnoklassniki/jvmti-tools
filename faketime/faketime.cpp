/*
 * Copyright 2020 Odnoklassniki Ltd, Mail.Ru Group
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
#include <stdlib.h>
#include <string.h>
#include <time.h>

static jlong (*real_time_millis)(JNIEnv*, jclass) = NULL;
static jlong (*real_nano_time_adjustment)(JNIEnv*, jclass, jlong) = NULL;
static jlong offset_millis = 0;

jlong JNICALL fake_time_millis(JNIEnv* env, jclass cls) {
    return real_time_millis(env, cls) + offset_millis;
}

jlong JNICALL fake_nano_time_adjustment(JNIEnv* env, jclass cls, jlong offset_seconds) {
    return real_nano_time_adjustment(env, cls, offset_seconds) + offset_millis * 1000000;
}

void JNICALL NativeMethodBind(jvmtiEnv* jvmti, JNIEnv* env, jthread thread, jmethodID method,
                              void* address, void** new_address_ptr) {
    char* name;
    if (jvmti->GetMethodName(method, &name, NULL, NULL) == 0) {
        if (real_time_millis == NULL && strcmp(name, "currentTimeMillis") == 0) {
            real_time_millis = (jlong (*)(JNIEnv*, jclass)) address;
            *new_address_ptr = (void*) fake_time_millis;
        } else if (real_nano_time_adjustment == NULL && strcmp(name, "getNanoTimeAdjustment") == 0) {
            real_nano_time_adjustment = (jlong (*)(JNIEnv*, jclass, jlong)) address;
            *new_address_ptr = (void*) fake_nano_time_adjustment;
        }
        jvmti->Deallocate((unsigned char*) name);
    }
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
    if (options != NULL) {
        if (options[0] == '+' || options[0] == '-') {
            offset_millis = atoll(options);                        // time offset
        } else {
            offset_millis = atoll(options) - time(NULL) * 1000LL;  // absolute time
        }
    }

    jvmtiEnv* jvmti;
    vm->GetEnv((void**) &jvmti, JVMTI_VERSION_1_0);

    jvmtiCapabilities capabilities = {0};
    capabilities.can_generate_native_method_bind_events = 1;
#if JNI_VERSION_9
    jvmtiCapabilities potential_capabilities;
    jvmti->GetPotentialCapabilities(&potential_capabilities);
    capabilities.can_generate_early_vmstart = potential_capabilities.can_generate_early_vmstart;
#endif
    jvmti->AddCapabilities(&capabilities);

    jvmtiEventCallbacks callbacks = {0};
    callbacks.NativeMethodBind = NativeMethodBind;
    jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));

    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_NATIVE_METHOD_BIND, NULL);

    return 0;
}
