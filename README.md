# JVM TI tools

Collection of small Java serviceability improvements based on
[JVM Tool Interface](https://docs.oracle.com/en/java/javase/11/docs/specs/jvmti.html).

 - [richNPE](#richnpe)
 - [vmtrace](#vmtrace)
 - [antimodule](#antimodule)
 - [heapsampler](#heapsampler)
 - [faketime](#faketime)

## richNPE

Enhances `NullPointerException` thrown by JVM with a detailed error message.

For example, there are several reasons why the following code may throw `NullPointerException`:

    long value = source.map().get(key);
    
Either

  - `source` is null, or
  - `map()` returns null, or
  - `get()` returns null and the subsequent unboxing fails.
  
The standard JDK exception message does not give a clue which expression exactly caused NPE,
but when using this tool, the message will look like

    java.lang.NullPointerException: Called method 'get()' on null object at bci 19

While [JDK-8218628](https://bugs.openjdk.java.net/browse/JDK-8218628) is going to be implemented
in JDK 13, the given agent improves NPE messages for existing JDK 8-12. 

#### Compilation

    # Linux
    g++ -O2 -fPIC -shared -I $JAVA_HOME/include -I $JAVA_HOME/include/linux -olibrichNPE.so richNPE.cpp
    
    # Windows
    cl /O2 /LD /I "%JAVA_HOME%/include" -I "%JAVA_HOME%/include/win32" richNPE.cpp

#### Usage

    java -agentpath:/path/to/librichNPE.so MainClass


## vmtrace

Traces basic JVM events like

 - Thread started / terminated
 - GC started / finished
 - Class loading / class prepared
 - Method compiled / unloaded
 - Dynamic code generated

#### Example output

```text
[0.05588] Method compiled: java/lang/String.<init> (1056 bytes)
[0.05597] Loading class: java/io/FileOutputStream$1 (557 bytes)
[0.05600] Class prepared: java/io/FileOutputStream$1
[0.05602] Method compiled: java/lang/String.hashCode (512 bytes)
[0.05618] Thread started: main
[0.05622] Loading class: sun/launcher/LauncherHelper (14692 bytes)
[0.05640] Dynamic code generated: I2C/C2I adapters(0xabbebebea0000000)@0x00000000032c38a0 (392 bytes)
[0.05642] Dynamic code generated: I2C/C2I adapters(0xbebebea0)@0x00000000032c36a0 (376 bytes)
...
```

#### Compilation

    # Linux
    g++ -O2 -fPIC -shared -I $JAVA_HOME/include -I $JAVA_HOME/include/linux -olibvmtrace.so vmtrace.cpp
    
    # Windows
    cl /O2 /LD /I "%JAVA_HOME%/include" -I "%JAVA_HOME%/include/win32" vmtrace.cpp

#### Usage

    java -agentpath:/path/to/libvmtrace.so[=output.log] MainClass

The log will be written to the file specified in the agent arguments,
or to `stderr` if no arguments given.


## antimodule

Removes Jigsaw restrictions in JDK 9+ by opening and exporting all
JDK modules to the unnamed module.

This allows Reflection access to all JDK private fields and methods
with no warnings, even when `--illegal-access=deny` option specified.
This also makes all JDK internal classes like `sun.nio.ch.DirectBuffer`
accessible by the unnamed module.

The agent is helpful for running older Java applications on JDK 9+
when application uses private APIs.

#### Compilation

    # Linux
    g++ -O2 -fPIC -shared -I $JAVA_HOME/include -I $JAVA_HOME/include/linux -olibantimodule.so antimodule.cpp
    
    # Windows
    cl /O2 /LD /I "%JAVA_HOME%/include" -I "%JAVA_HOME%/include/win32" antimodule.cpp

#### Usage

    java -agentpath:/path/to/libantimodule.so MainClass


## heapsampler

The example of low-overhead heap allocation profiler based on
[JEP 331](https://openjdk.java.net/jeps/331).

Requires JDK 11 or later.

#### Example output

The output is generated in collapsed stacktraces format suitable for
generating [Flame Graphs](https://github.com/brendangregg/FlameGraph/).

```text
Allocate.main;java.lang.Long.valueOf;java.lang.Long 49
Allocate.main;java.lang.Object[] 31
java.lang.Thread.run;jdk.internal.misc.Signal$1.run;java.lang.Terminator$1.handle;java.lang.Class 1
jdk.internal.misc.Signal.dispatch;java.lang.Class 1
```

See [async-profiler](https://github.com/jvm-profiling-tools/async-profiler)
for more information about allocation profiling and Flame Graphs.
Note that `heapsampler` works only on JDK 11+, while `async-profiler`
is capable of generating allocation profiles on JDK 7+.

#### Compilation

    # Linux
    g++ -O2 -fPIC -shared -I $JAVA_HOME/include -I $JAVA_HOME/include/linux -olibheapsampler.so heapsampler.cpp
    
    # Windows
    cl /O2 /LD /I "%JAVA_HOME%/include" -I "%JAVA_HOME%/include/win32" heapsampler.cpp

#### Usage

    java -agentpath:/path/to/libheapsampler.so[=interval] MainClass > output.txt

The agent can be also loaded dynamically in run-time:

    jcmd <pid> JVMTI.agent_load /path/to/libheapsampler.so [interval]

The optional `interval` argument specifies the sampling interval in bytes.
The default value is 512 KB.
The output is printed on `stdout`.


## faketime

Changes current date/time for a Java application without affecting system date/time.

The agent rebinds the native entry for `System.currentTimeMillis` and related methods
and adds the specified offset to the times returned by these methods.

#### Compilation

    # Linux
    g++ -O2 -fPIC -shared -I $JAVA_HOME/include -I $JAVA_HOME/include/linux -olibfaketime.so faketime.cpp
    
    # Windows
    cl /O2 /LD /I "%JAVA_HOME%/include" -I "%JAVA_HOME%/include/win32" faketime.cpp

#### Usage

    java -agentpath:/path/to/libfaketime.so=timestamp|+-offset MainClass

where the agent argument is either

 - absolute `timestamp` in milliseconds from Epoch, or
 - relative `offset` in milliseconds, if `offset` starts with `+` or `-`

Since `System.currentTimeMillis` is a JVM intrinsic method, it's also required to disable
the corresponding intrinsic to make sure the JNI method is called:

    java -XX:+UnlockDiagnosticVMOptions -XX:DisableIntrinsic=_currentTimeMillis -XX:CompileCommand=dontinline,java.lang.System::currentTimeMillis
