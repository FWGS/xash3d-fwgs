# GetNativeObject API

To be able to use platform-specific features or get optional engine interfaces, we've added a simple call to MobilityAPI on client DLL and PhysicsAPI for server DLL and extended MenuAPI for menu DLL.

It's defined like this:

```
void *pfnGetNativeObject( const char *name );
```

#### Cross-platform objects

Only these objects are guaranteed to be available on all targets.

| Object name | Interface |
|-------------|-----------|
| `VFileSystem009` | Provides C++ interface to filesystem, binary-compatible with Valve's VFileSystem009. |
| `XashFileSystemXXX` | Provides C interface to filesystem. This interface is unstable and not recommended for generic use, outside of engine internals. For more info about current version look into `filesystem.h`. |

#### Android-specific objects

| Object name | Interface |
|-------------|-----------|
| `JNIEnv`    | Allows interfacing with Java Native Interface. |
| `ActivityClass` | Returns JNI object for engine Android activity class. |
