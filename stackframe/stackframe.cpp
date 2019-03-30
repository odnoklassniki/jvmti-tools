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
#include <cstdio>
#include <map>

class MethodInfo {
  private:
    char* _file = NULL;
    jvmtiLineNumberEntry* _line_number_table = NULL;
    jint _table_size = 0;
    bool _initialized = false;

  public:
    bool initialized() {
        return _initialized;
    }

    void initialize(jvmtiEnv* jvmti, jmethodID method) {
        jclass method_class;
        if (jvmti->GetMethodDeclaringClass(method, &method_class) == 0) {
            jvmti->GetSourceFileName(method_class, &_file);
        }

        jvmti->GetLineNumberTable(method, &_table_size, &_line_number_table);

        _initialized = true;
    }

    char* file() {
        return _file;
    }

    int line(jlocation location) {
        int line = 0;
        jlocation min = 0xffff;
        for (int i = 0; i < _table_size; i++) {
            jlocation start = _line_number_table[i].start_location;
            if (location >= start && start < min) {
                line = _line_number_table[i].line_number;
                min = start;
            }
        }
        return line;
    }
};

static jvmtiEnv* jvmti;
static std::map<jmethodID, MethodInfo> method_cache;

extern "C" JNIEXPORT jstring JNICALL
Java_StackFrame_getLocation(JNIEnv* env, jclass unused, jint depth) {
    jvmtiFrameInfo frame;
    jint count;
    if (jvmti->GetStackTrace(NULL, depth, 1, &frame, &count) != 0) {
        return NULL;
    }

    MethodInfo* info = &method_cache[frame.method];
    if (!info->initialized()) {
        info->initialize(jvmti, frame.method);
    }

    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s:%d", info->file(), info->line(frame.location));
    return env->NewStringUTF(buf);
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    vm->GetEnv((void**) &jvmti, JVMTI_VERSION_1_0);

    jvmtiCapabilities capabilities = {0};
    capabilities.can_get_source_file_name = 1;
    capabilities.can_get_line_numbers = 1;
    jvmti->AddCapabilities(&capabilities);

    return JNI_VERSION_1_6;
}
