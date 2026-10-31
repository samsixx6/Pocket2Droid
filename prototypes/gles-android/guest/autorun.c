/* Keep the 2577 entry short: networking/setup run as independent CE processes. */
#include "ce_api.h"
static void start(const W *path){U32 process[4];p2d_zero(process,sizeof(process));if(!p2d_exists(path))return;if(CreateProcessW(path,0,0,0,0,0,0,0,0,process)){if(process[0])CloseHandle((H)process[0]);if(process[1])CloseHandle((H)process[1]);}}
void _start(void){start(L"\\Storage Card\\P2DGLES\\network.exe");start(L"\\Storage Card\\P2DGLES\\setup.exe");ExitThread(0);}
