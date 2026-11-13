#include "b4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
static void pause_ms(void) { Sleep(10); }
#else
#include <unistd.h>
static void pause_ms(void) { usleep(10000); }
#endif
#define NEED(x) do { if(!(x)) { fprintf(stderr,"B4_FAIL line=%d: %s\n",__LINE__,#x); return 1; } } while(0)
static b4_record record(unsigned n) {
    b4_record v={0}; size_t i;
    v.id=17+n; v.sequence_number=100+n;
    v.label_size=n%2?64:0; v.payload_size=n%2?256:0;
    for(i=0;i<v.label_size;i++) v.label[i]=(char)('a'+n);
    for(i=0;i<v.payload_size;i++) v.payload[i]=(uint8_t)(i+n);
    return v;
}
static int equal(const b4_record *a,const b4_record *b) {
    return a->id==b->id && a->sequence_number==b->sequence_number && a->label_size==b->label_size && a->payload_size==b->payload_size &&
        memcmp(a->label,b->label,a->label_size)==0 && memcmp(a->payload,b->payload,a->payload_size)==0;
}
static int ownership(void) {
    b4_sample *s=NULL; b4_record v=record(1),out={0},before;
    b4_endpoint *e=NULL; b4_options bad={0,NULL,1,0};
    NEED(b4_sample_create(NULL)==B4_BAD_PARAMETER);
    NEED(b4_sample_create(&s)==B4_OK); NEED(b4_sample_create(&s)==B4_BAD_PARAMETER);
    NEED(b4_sample_get(s,&out)==B4_OK && out.label_size==0 && out.payload_size==0);
    {
        b4_record empty=record(0);
        NEED(b4_sample_set(s,&empty)==B4_OK);
        NEED(b4_sample_get(s,&out)==B4_OK && equal(&out,&empty));
    }
    NEED(b4_sample_set(s,&v)==B4_OK); memset(&v,0,sizeof(v));
    NEED(b4_sample_get(s,&out)==B4_OK); before=record(1); NEED(equal(&out,&before));
    v=before; v.label_size=65; NEED(b4_sample_set(s,&v)==B4_BOUNDS);
    v=before; v.payload_size=257; NEED(b4_sample_set(s,&v)==B4_BOUNDS);
    v=before; v.label[0]=0; NEED(b4_sample_set(s,&v)==B4_BAD_PARAMETER);
    NEED(b4_sample_get(s,&out)==B4_OK && equal(&out,&before));
    NEED(b4_sample_set(NULL,&v)==B4_BAD_PARAMETER); NEED(b4_sample_get(NULL,&out)==B4_BAD_PARAMETER);
    NEED(b4_open(&bad,&e)==B4_BAD_PARAMETER && e==NULL);
    NEED(b4_wait_match(NULL,0)==B4_BAD_PARAMETER); NEED(b4_take(NULL,s)==B4_BAD_PARAMETER);
    NEED(b4_write(NULL,s)==B4_BAD_PARAMETER); NEED(b4_wait_ack(NULL,0)==B4_BAD_PARAMETER);
    NEED(b4_sample_destroy(s)==B4_OK); NEED(b4_sample_destroy(NULL)==B4_OK); NEED(b4_close(NULL)==B4_OK);
    puts("B4_OWNERSHIP PASS"); return 0;
}
int main(int argc,char **argv) {
    b4_sample *s=NULL; b4_endpoint *e=NULL; b4_options o;
    unsigned seen=0,n; b4_record v,out; time_t until;
    if(argc==1) return ownership();
    /* role domain topic rep [token run local_udid peer_udid network side gate] */
    NEED(argc==5 || argc==12); NEED(strcmp(argv[1],"writer")==0 || strcmp(argv[1],"reader")==0);
    o.writer=strcmp(argv[1],"writer")==0; o.domain=(uint32_t)strtoul(argv[2],NULL,10); o.topic=argv[3]; o.representation=atoi(argv[4]);
    NEED(b4_sample_create(&s)==B4_OK);
    if(argc==12) {
        b4_dsoftbus_options d={argv[5],argv[6],argv[7],argv[8],argv[9],argv[11],(unsigned)strtoul(argv[10],NULL,10)};
        NEED(b4_open_dsoftbus(&o,&d,&e)==B4_OK);
    } else NEED(b4_open(&o,&e)==B4_OK);
    NEED(b4_wait_match(e,15000)==B4_OK);
    if(o.writer) {
        NEED(b4_take(e,s)==B4_BAD_PARAMETER);
        for(n=0;n<10;n++) { v=record(n); NEED(b4_sample_set(s,&v)==B4_OK); NEED(b4_write(e,s)==B4_OK); }
        NEED(b4_wait_ack(e,5000)==B4_OK);
    } else {
        NEED(b4_write(e,s)==B4_BAD_PARAMETER); NEED(b4_wait_ack(e,0)==B4_BAD_PARAMETER);
        until=time(NULL)+15;
        while(seen!=1023 && time(NULL)<until) {
            b4_result code=b4_take(e,s);
            NEED(code==B4_OK || code==B4_NO_DATA);
            if(code==B4_OK) {
                NEED(b4_sample_get(s,&out)==B4_OK && out.id>=17 && out.id<27);
                n=out.id-17; v=record(n); NEED(equal(&v,&out)); seen|=1u<<n;
            } else pause_ms();
        } NEED(seen==1023);
    }
    {
        b4_transport_counters counters={0,0};
        if(argc==12) {
            NEED(b4_transport_stats(e,&counters)==B4_OK);
            NEED(counters.sdk_sends>0 && counters.received_records>0);
            printf("B4_SOCKET sdk_sends=%llu received_records=%llu\n",(unsigned long long)counters.sdk_sends,(unsigned long long)counters.received_records);
        } else NEED(b4_transport_stats(e,&counters)==B4_UNSUPPORTED);
    }
    NEED(b4_close(e)==B4_OK); NEED(b4_sample_destroy(s)==B4_OK);
    printf("B4_DDS PASS role=%s rep=%d transport=%s\n",argv[1],o.representation,argc==12?"dsoftbus":"udp"); return 0;
}
