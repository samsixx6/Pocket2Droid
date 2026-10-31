#ifndef P2D_D3DM_CE_SDK_COMPAT_H
#define P2D_D3DM_CE_SDK_COMPAT_H
#include <stdint.h>
typedef uint8_t BYTE;
typedef uint16_t WORD, WCHAR, TCHAR;
typedef uint32_t DWORD, ULONG, UINT;
typedef int32_t LONG, INT, HRESULT, BOOL;
typedef void VOID;
typedef void *HANDLE, *HWND, *HDC;
typedef union { struct { DWORD LowPart; LONG HighPart; }; int64_t QuadPart; } LARGE_INTEGER;
typedef struct { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; } GUID;
typedef GUID IID;
typedef const IID *REFIID;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;
typedef struct _RGNDATA RGNDATA;
#define WINAPI
#define CONST const
#define TRUE 1
#define FALSE 0
#define interface struct
#define PURE
#define THIS INTERFACE *This
#define THIS_ INTERFACE *This,
#define STDMETHOD(name) HRESULT (*name)
#define STDMETHOD_(type,name) type (*name)
#define DECLARE_INTERFACE_(name,base) \
 struct name { const struct name##Vtbl *lpVtbl; }; \
 typedef struct name##Vtbl name##Vtbl; struct name##Vtbl
#ifdef INITGUID
#define DEFINE_GUID(n,a,b,c,d,e,f,g,h,i,j,k) const GUID n={a,b,c,{d,e,f,g,h,i,j,k}}
#else
#define DEFINE_GUID(n,a,b,c,d,e,f,g,h,i,j,k) extern const GUID n
#endif
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005u)
#define E_NOINTERFACE ((HRESULT)0x80004002u)
#define E_POINTER ((HRESULT)0x80004003u)
#define E_OUTOFMEMORY ((HRESULT)0x8007000eu)
#define MAKE_HRESULT(s,f,c) ((HRESULT)(((uint32_t)(s)<<31)|((uint32_t)(f)<<16)|(c)))
#define SUCCEEDED(h) ((HRESULT)(h)>=0)
#define FAILED(h) ((HRESULT)(h)<0)
#endif
