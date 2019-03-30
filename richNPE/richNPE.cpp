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
#include <classfile_constants.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u1;
typedef unsigned short u2;

static const char* get_exception_message(u1 bytecode) {
    switch (bytecode) {
        case JVM_OPC_iaload: return "Load from null int array at bci %d";
        case JVM_OPC_laload: return "Load from null long array at bci %d";
        case JVM_OPC_faload: return "Load from null float array at bci %d";
        case JVM_OPC_daload: return "Load from null double array at bci %d";
        case JVM_OPC_aaload: return "Load from null Object array at bci %d";
        case JVM_OPC_baload: return "Load from null byte/boolean array at bci %d";
        case JVM_OPC_caload: return "Load from null char array at bci %d";
        case JVM_OPC_saload: return "Load from null short array at bci %d";

        case JVM_OPC_iastore: return "Store into null int array at bci %d";
        case JVM_OPC_lastore: return "Store into null long array at bci %d";
        case JVM_OPC_fastore: return "Store into null float array at bci %d";
        case JVM_OPC_dastore: return "Store into null double array at bci %d";
        case JVM_OPC_aastore: return "Store into null Object array at bci %d";
        case JVM_OPC_bastore: return "Store into null byte/boolean array at bci %d";
        case JVM_OPC_castore: return "Store into null char array at bci %d";
        case JVM_OPC_sastore: return "Store into null short array at bci %d";

        case JVM_OPC_arraylength: return "Get .length of null array";

        case JVM_OPC_getfield: return "Get field '%s' of null object at bci %d";
        case JVM_OPC_putfield: return "Put field '%s' of null object at bci %d";

        case JVM_OPC_invokevirtual: // fall through
        case JVM_OPC_invokespecial: // fall through
        case JVM_OPC_invokeinterface: return "Called method '%s' on null object at bci %d";

        case JVM_OPC_monitorenter: // fall through
        case JVM_OPC_monitorexit: return "Synchronized on null monitor at bci %d";

        default: return NULL;
    }
}

static u2 get_u2(const u1* bytes) {
    return bytes[0] << 8 | bytes[1];
}

static u1* get_cpool_at(u1* cpool, u2 index) {
    // Length in bytes of a constant pool item with the given tag
    static u1 cp_item_size[] = {0, 3, 0, 5, 5, 9, 9, 3, 3, 5, 5, 5, 5, 4, 3, 5, 5, 3, 3};

    for (unsigned int i = 1; i < index; i++) {
        u1 tag = cpool[0];
        cpool += tag == JVM_CONSTANT_Utf8 ? 3 + get_u2(cpool + 1) : cp_item_size[tag];
    }

    return cpool;
}

static char* get_name_from_cpool(jvmtiEnv* jvmti, jmethodID method, const u1* bytecodes) {
    jclass holder;
    jvmti->GetMethodDeclaringClass(method, &holder);

    jint cpool_count;
    jint cpool_bytes;
    u1* cpool;
    if (jvmti->GetConstantPool(holder, &cpool_count, &cpool_bytes, &cpool) != 0) {
        return strdup("<unknown>");
    }

    u1* ref = get_cpool_at(cpool, get_u2(bytecodes + 1));       // CONSTANT_Fieldref / Methodref
    u1* name_and_type = get_cpool_at(cpool, get_u2(ref + 3));   // CONSTANT_NameAndType
    u1* name = get_cpool_at(cpool, get_u2(name_and_type + 1));  // CONSTANT_Utf8

    size_t name_length = get_u2(name + 1);
    char* result = (char*) malloc(name_length + 1);
    memcpy(result, name + 3, name_length);
    result[name_length] = 0;

    jvmti->Deallocate(cpool);
    return result;
}


// Cache JNI handles as soon as VM initializes
static jclass NullPointerException = NULL;
static jfieldID detailMessage = NULL;

void JNICALL VMInit(jvmtiEnv* jvmti, JNIEnv* env, jthread thread) {
    jclass localNPE = env->FindClass("java/lang/NullPointerException");
    NullPointerException = (jclass) env->NewGlobalRef(localNPE);

    jclass Throwable = env->FindClass("java/lang/Throwable");
    detailMessage = env->GetFieldID(Throwable, "detailMessage", "Ljava/lang/String;");
}

void JNICALL ExceptionCallback(jvmtiEnv* jvmti, JNIEnv* env, jthread thread,
                               jmethodID method, jlocation location, jobject exception,
                               jmethodID catch_method, jlocation catch_location) {

    if (NullPointerException == NULL || detailMessage == NULL ||
        !env->IsInstanceOf(exception, NullPointerException)) {
        return;
    }

    jint bytecode_count;
    u1* bytecodes;
    if (jvmti->GetBytecodes(method, &bytecode_count, &bytecodes) != 0) {
        return;
    }

    if (location >= 0 && location < bytecode_count) {
        const char* message = get_exception_message(bytecodes[location]);
        if (message != NULL) {
            char buf[400];
            if (strstr(message, "%s") != NULL) {
                char* name = get_name_from_cpool(jvmti, method, bytecodes + location);
                snprintf(buf, sizeof(buf), message, name, (int) location);
                free(name);
            } else {
                sprintf(buf, message, (int) location);
            }
            env->SetObjectField(exception, detailMessage, env->NewStringUTF(buf));
        }
    }

    jvmti->Deallocate(bytecodes);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
    jvmtiEnv* jvmti;
    vm->GetEnv((void**) &jvmti, JVMTI_VERSION_1_0);

    jvmtiCapabilities capabilities = {0};
    capabilities.can_generate_exception_events = 1;
    capabilities.can_get_bytecodes = 1;
    capabilities.can_get_constant_pool = 1;
    jvmti->AddCapabilities(&capabilities);

    jvmtiEventCallbacks callbacks = {0};
    callbacks.VMInit = VMInit;
    callbacks.Exception = ExceptionCallback;
    jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_EXCEPTION, NULL);

    return 0;
}
