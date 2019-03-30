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
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <iostream>

#define MAX_STACK_DEPTH 1024

struct Frame {
    jlong samples;
    jlong bytes;
    std::map<jmethodID, Frame> children;
};

static jvmtiEnv* jvmti = NULL;
static jrawMonitorID tree_lock;
static std::map<std::string, Frame> root;

// Converts JVM internal class signature to human readable name
static std::string decode_class_signature(char* class_sig) {
    switch (class_sig[0]) {
        case 'B': return "byte";
        case 'C': return "char";
        case 'S': return "short";
        case 'I': return "int";
        case 'J': return "long";
        case 'F': return "float";
        case 'D': return "double";
        case 'Z': return "boolean";
        case '[': return decode_class_signature(class_sig + 1) + "[]";
    }

    // Strip 'L' and ';'
    class_sig++;
    class_sig[std::strlen(class_sig) - 1] = 0;

    // Replace '/' with '.'
    for (char* c = class_sig; *c; c++) {
        if (*c == '/') *c = '.';
    }

    return class_sig;
}

static std::string get_method_name(jmethodID method) {
    jclass method_class;
    char* class_sig = NULL;
    char* method_name = NULL;
    std::string result;

    if (jvmti->GetMethodDeclaringClass(method, &method_class) == 0 &&
        jvmti->GetClassSignature(method_class, &class_sig, NULL) == 0 &&
        jvmti->GetMethodName(method, &method_name, NULL, NULL) == 0) {
        result.assign(decode_class_signature(class_sig) + "." + method_name);
    } else {
        result.assign("[unknown]");
    }

    jvmti->Deallocate((unsigned char*) method_name);
    jvmti->Deallocate((unsigned char*) class_sig);
    return result;
}

static void dump_tree(const std::string stack_line, const std::string& class_name, const Frame* f) {
    if (f->samples > 0) {
        // Output sample in 'collapsed stack traces' format understood by flamegraph.pl
        std::cout << stack_line << class_name << "_[i] " << f->samples << std::endl;
    }
    for (auto it = f->children.begin(); it != f->children.end(); ++it) {
        dump_tree(stack_line + get_method_name(it->first) + ";", class_name, &it->second);
    }
}

static void dump_profile() {
    for (auto it = root.begin(); it != root.end(); ++it) {
        dump_tree("", it->first, &it->second);
    }
}

static void record_stack_trace(char* class_sig, jvmtiFrameInfo* frames, jint count, jlong size) {
    Frame* f = &root[decode_class_signature(class_sig)];
    while (--count >= 0) {
        f = &f->children[frames[count].method];
    }
    f->samples++;
    f->bytes += size;
}

void JNICALL SampledObjectAlloc(jvmtiEnv* jvmti, JNIEnv* env, jthread thread,
                                jobject object, jclass object_klass, jlong size) {

    jvmtiFrameInfo frames[MAX_STACK_DEPTH];
    jint count;
    if (jvmti->GetStackTrace(thread, 0, MAX_STACK_DEPTH, frames, &count) != 0) {
        return;
    }

    char* class_sig;
    if (jvmti->GetClassSignature(object_klass, &class_sig, NULL) != 0) {
        return;
    }

    jvmti->RawMonitorEnter(tree_lock);
    record_stack_trace(class_sig, frames, count, size);
    jvmti->RawMonitorExit(tree_lock);

    jvmti->Deallocate((unsigned char*) class_sig);
}

void JNICALL DataDumpRequest(jvmtiEnv* jvmti) {
    jvmti->RawMonitorEnter(tree_lock);
    dump_profile();
    jvmti->RawMonitorExit(tree_lock);
}

void JNICALL VMDeath(jvmtiEnv* jvmti, JNIEnv* env) {
    DataDumpRequest(jvmti);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* vm, char* options, void* reserved) {
    vm->GetEnv((void**) &jvmti, JVMTI_VERSION_1_0);

    jvmti->CreateRawMonitor("tree_lock", &tree_lock);

    jvmtiCapabilities capabilities = {0};
    capabilities.can_generate_sampled_object_alloc_events = 1;
    jvmti->AddCapabilities(&capabilities);

    if (options != NULL && options[0] >= '0' && options[0] <= '9') {
        jvmti->SetHeapSamplingInterval(std::atoi(options));
    }

    jvmtiEventCallbacks callbacks = {0};
    callbacks.SampledObjectAlloc = SampledObjectAlloc;
    callbacks.DataDumpRequest = DataDumpRequest;
    callbacks.VMDeath = VMDeath;
    jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_SAMPLED_OBJECT_ALLOC, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_DATA_DUMP_REQUEST, NULL);
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_DEATH, NULL);

    return 0;
}

JNIEXPORT jint JNICALL Agent_OnAttach(JavaVM* vm, char* options, void* reserved) {
    // Protect against repeated load
    if (jvmti != NULL) {
        return 0;
    }
    return Agent_OnLoad(vm, options, reserved);
}
