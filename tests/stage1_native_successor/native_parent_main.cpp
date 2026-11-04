#include "control/parent_protocol.hpp"
#include <cstdio>
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);return stage1_control::native_parent(argc,argv);}
