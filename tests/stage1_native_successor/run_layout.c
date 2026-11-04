#include "run_contract.h"
_Static_assert(sizeof(NS_Envelope)==320,"ARM64 envelope C layout");
_Static_assert(sizeof(NS_RunEvent)==424,"ARM64 event C layout");
_Static_assert(sizeof(NS_RunControl)==56,"ARM64 control C layout");
_Static_assert(sizeof(NS_RunApi)==48,"ARM64 run API C layout");
_Static_assert(sizeof(NS_RunResult)==48,"ARM64 result C layout");
int ns_run_c_layout_check(void){return 1;}
