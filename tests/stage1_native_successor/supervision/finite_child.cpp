#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
int main(int argc,char** argv){if(argc==2&&!std::strcmp(argv[1],"--exit-zero"))return 0;if(argc==2&&!std::strcmp(argv[1],"--finite-wait")){Sleep(750);return 0;}return 91;}
