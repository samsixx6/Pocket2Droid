/* Host test only: reuse existing real JNI I/O faults, optionally exit as a crash. */
#define p2d_test_fsync original_test_fsync
#include "storage_faults.c"
#undef p2d_test_fsync
static int crash_on_fault;
int p2d_test_fsync(int fd){int result=original_test_fsync(fd);if(result<0&&crash_on_fault)_exit(93);return result;}
JNIEXPORT void JNICALL Java_com_samsixx_pocket2droid_RomUpdateTest_directoryFault
 (JNIEnv*env,jclass type,jstring path,jint fault,jint nth){Java_com_samsixx_pocket2droid_StorageFallbackTest_directoryFault(env,type,path,fault,nth);}
JNIEXPORT jint JNICALL Java_com_samsixx_pocket2droid_RomUpdateTest_syncCalls
 (JNIEnv*env,jclass type){(void)env;(void)type;return sync_calls;}
JNIEXPORT void JNICALL Java_com_samsixx_pocket2droid_RomUpdateTest_crashFault
 (JNIEnv*env,jclass type,jboolean value){(void)env;(void)type;crash_on_fault=value;}
JNIEXPORT void JNICALL Java_com_samsixx_pocket2droid_RomUpdateTest_failAfter
 (JNIEnv*env,jclass type,jlong bytes){Java_com_samsixx_pocket2droid_StorageFallbackTest_failAfter(env,type,bytes);}

#include <dlfcn.h>
static char file_sync_path[4096];static int file_sync_fault,file_sync_calls;
static int actual_sync(int fd,int data){int (*real)(int)=dlsym(RTLD_NEXT,data?"fdatasync":"fsync");struct stat st;char link[64],path[4096];if(file_sync_fault&&fstat(fd,&st)==0&&S_ISREG(st.st_mode)){snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);ssize_t n=readlink(link,path,sizeof(path)-1);if(n>=0){path[n]=0;if(!strcmp(path,file_sync_path)){file_sync_calls++;errno=EIO;return -1;}}}return real(fd);}
int fsync(int fd){return actual_sync(fd,0);}int fdatasync(int fd){return actual_sync(fd,1);}
JNIEXPORT void JNICALL Java_com_samsixx_pocket2droid_RomUpdateTest_fileFault(JNIEnv*env,jclass type,jstring path,jboolean enabled){(void)type;const char*p=(*env)->GetStringUTFChars(env,path,NULL);if(!p)return;snprintf(file_sync_path,sizeof(file_sync_path),"%s",p);(*env)->ReleaseStringUTFChars(env,path,p);file_sync_fault=enabled;file_sync_calls=0;}
JNIEXPORT jint JNICALL Java_com_samsixx_pocket2droid_RomUpdateTest_fileSyncCalls(JNIEnv*env,jclass type){(void)env;(void)type;return file_sync_calls;}
