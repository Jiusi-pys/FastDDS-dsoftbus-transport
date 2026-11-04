#include "native_contract.h"
_Static_assert(sizeof(void*)==8,"ARM64 LP64");
_Static_assert(sizeof(NS_Header)==16&&sizeof(NS_Observation)==208,"C header/observation");
_Static_assert(sizeof(NS_LoaderSlot)==56&&sizeof(NS_IdentityApi)==24&&sizeof(NS_LoaderApi)==32,"C API layout");
_Static_assert(offsetof(NS_Observation,process_name)==68&&offsetof(NS_LoaderSlot,ctor_ns)==40,"C offsets");
int ns_c_layout_check(void){return sizeof(NS_ConsumerApi)==32&&sizeof(NS_Result)==32;}
