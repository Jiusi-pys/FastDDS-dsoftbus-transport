#include "contract.h"
#include <type_traits>
static_assert(sizeof(void*)==8,"x64 Host ABI only");
static_assert(sizeof(DD_Api)==24&&sizeof(DD_Host)==48&&sizeof(DD_Request)==64&&sizeof(DD_Result)==40,"Host v1 exact layout");
static_assert(offsetof(DD_Request,input)==24&&offsetof(DD_Request,output)==40&&offsetof(DD_Request,scenario)==56,"pointer/length offsets");
static_assert(std::is_standard_layout<DD_Request>::value&&std::is_trivially_copyable<DD_Request>::value,"POD request");
static_assert(std::is_standard_layout<DD_Result>::value&&std::is_trivially_copyable<DD_Result>::value,"POD result");
/* Compile-only assertions: no C++ global initializer, SDK or identity provider. */
