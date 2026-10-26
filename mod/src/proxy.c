/* dinput.dll proxy: Thief2MP.exe does LoadLibraryA("dinput.dll"), which finds this file in the
 * game folder first. Every export forwards to the system dinput.dll. */
#include <windows.h>

static HMODULE real;

static FARPROC get(const char *name)
{
    if (!real) {
        char path[MAX_PATH];
        UINT n = GetSystemDirectoryA(path, MAX_PATH);   /* SysWOW64 for a 32-bit process */
        lstrcpyA(path + n, "\\dinput.dll");
        real = LoadLibraryA(path);
    }
    return real ? GetProcAddress(real, name) : NULL;
}

#define FWD(name, params, args)                                                 \
    HRESULT WINAPI p_##name params                                              \
    {                                                                           \
        typedef HRESULT(WINAPI * fn_t) params;                                  \
        fn_t f = (fn_t)get(#name);                                              \
        return f ? f args : E_FAIL;                                             \
    }

FWD(DirectInputCreateA, (HINSTANCE h, DWORD v, void **o, IUnknown *u), (h, v, o, u))
FWD(DirectInputCreateW, (HINSTANCE h, DWORD v, void **o, IUnknown *u), (h, v, o, u))
FWD(DirectInputCreateEx, (HINSTANCE h, DWORD v, REFIID r, void **o, IUnknown *u), (h, v, r, o, u))
FWD(DllCanUnloadNow, (void), ())
FWD(DllGetClassObject, (REFCLSID c, REFIID r, void **o), (c, r, o))
FWD(DllRegisterServer, (void), ())
FWD(DllUnregisterServer, (void), ())
