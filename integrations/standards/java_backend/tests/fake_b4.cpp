// Synthetic JNI ownership fault backend. Never used for physical DDS acceptance.
#include "b4.h"
#include <cstring>
struct b4_endpoint { unsigned attempts=0; };
struct b4_sample {b4_record record{};};
extern "C" b4_result b4_open(const b4_options* options,b4_endpoint** out) {
    try {
        *out=new b4_endpoint;
        if(std::strcmp(options->topic,"throw-failure")==0)(*out)->attempts=1;
        return std::strcmp(options->topic,"open-failure")==0?B4_ERROR:B4_OK;
    }catch(...){return B4_ERROR;}
}
extern "C" b4_result b4_close(b4_endpoint* endpoint) {
    if(!endpoint)return B4_OK;
    if(++endpoint->attempts==1)return B4_ERROR;
    delete endpoint;return B4_OK;
}
extern "C" b4_result b4_sample_create(b4_sample** out){try{*out=new b4_sample;return B4_OK;}catch(...){return B4_ERROR;}}
extern "C" b4_result b4_sample_destroy(b4_sample* sample){delete sample;return B4_OK;}
extern "C" b4_result b4_sample_set(b4_sample* sample,const b4_record* value){sample->record=*value;return B4_OK;}
extern "C" b4_result b4_sample_get(const b4_sample* sample,b4_record* value){*value=sample->record;return B4_OK;}
extern "C" b4_result b4_wait_match(b4_endpoint*,uint32_t){return B4_TIMEOUT;}
extern "C" b4_result b4_write(b4_endpoint*,const b4_sample*){return B4_OK;}
extern "C" b4_result b4_take(b4_endpoint*,b4_sample*){return B4_NO_DATA;}
extern "C" b4_result b4_wait_ack(b4_endpoint*,uint32_t){return B4_OK;}
