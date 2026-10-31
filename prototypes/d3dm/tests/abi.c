#include <stddef.h>
#include "d3dm.h"
_Static_assert(D3DM_SDK_VERSION == 120, "SDK120");
_Static_assert(sizeof(DWORD)==4 && sizeof(ULONG)==4 && sizeof(LONG)==4, "CE scalar sizes");
_Static_assert(sizeof(GUID)==16, "GUID size");
_Static_assert(sizeof(D3DMPRESENT_PARAMETERS)==44, "presentation size");
_Static_assert(sizeof(D3DMVIEWPORT)==24, "viewport size");
_Static_assert(sizeof(D3DMMATRIX)==64, "matrix size");
_Static_assert(sizeof(D3DMVERTEXBUFFER_DESC)==24, "vertex descriptor size");
_Static_assert(sizeof(D3DMSURFACE_DESC)==32, "surface descriptor size");
_Static_assert(offsetof(IDirect3DMobileVtbl,CreateDevice)==16*sizeof(void*), "CreateDevice slot");
_Static_assert(offsetof(IDirect3DMobileDeviceVtbl,Present)==12*sizeof(void*), "Present slot");
_Static_assert(offsetof(IDirect3DMobileDeviceVtbl,DrawPrimitive)==53*sizeof(void*), "Draw slot");
_Static_assert(offsetof(IDirect3DMobileDeviceVtbl,DrawIndexedPrimitive)==54*sizeof(void*), "Indexed slot");
_Static_assert(sizeof(IDirect3DMobileDeviceVtbl)==62*sizeof(void*), "Complete device ABI");
int abi_fixture(void){return D3DM_SDK_VERSION;}
