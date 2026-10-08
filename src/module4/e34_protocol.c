#include "wrj_receiver.h"
#include <stdlib.h>
#include <time.h>
static const uint16_t magic[6]={0xD1C2U,0xD1B4U,0xD10DU,0xA1C2U,0xA1B4U,0x7E7EU};
static const uint8_t code[6]={1U,2U,3U,4U,5U,15U};
static uint32_t be(const uint8_t *b,uint32_t n)
{uint32_t v=0U;for(uint32_t k=0U;k<n;++k)v=(v<<8U)|b[k];return v;}
const char *wrj_protocol_name(wrj_protocol_t p)
{
    static const char *names[]={"auto","DJI_Control_Link","DJI_Wideband_Link","DJI_DroneID",
        "Autel_Control_Link","Autel_Wideband_Link","Unknown_UAV_Link","RemoteID_BLE"};
    return p<=WRJ_PROTOCOL_REMOTEID ? names[p] : "INVALID";
}
uint16_t wrj_crc16_received(const uint8_t *b,uint32_t n)
{
    uint16_t crc=0xffffU;
    for(uint32_t j=0U;j<n;++j){crc^=(uint16_t)((uint16_t)b[j]<<8U);
        for(uint32_t k=0U;k<8U;++k)crc=(uint16_t)((crc<<1U)^((crc&0x8000U)?0x1021U:0U));}
    return crc;
}
uint32_t wrj_crc24_received(const uint8_t *b,uint32_t n)
{
    uint32_t crc=0x555555U;
    for(uint32_t j=0U;j<n;++j)for(uint32_t k=0U;k<8U;++k){
        const uint32_t f=((crc>>23U)&1U)^((b[j]>>k)&1U);
        crc=((crc<<1U)&0xffffffU)^(f?0x65bU:0U);}
    return crc;
}
static void field(wrj_rx_record_t *r,const char *name,uint32_t a,uint32_t b,double value)
{
    if(r->field_count>=WRJ_RX_MAX_FIELDS)return;
    wrj_rx_field_t *f=&r->fields[r->field_count++];
    memset(f,0,sizeof(*f));
    snprintf(f->data_type,sizeof(f->data_type),"engineering_binary");
    snprintf(f->validation_status,sizeof(f->validation_status),"CRC16_PASS");
    snprintf(f->semantic,sizeof(f->semantic),"%s",name);f->start_byte=(uint16_t)a;
    f->end_byte=(uint16_t)b;f->numeric_value=value;
}
int wrj_protocol_parse(wrj_rx_record_t *r)
{
    const uint8_t *b=r->bytes;const uint32_t n=r->byte_count;
    r->frame_valid=r->crc_valid=r->semantic_complete=0U;r->field_count=0U;
    if(n<13U || n>WRJ_RX_MAX_BYTES)return 0;
    int index=-1;
    for(uint32_t k=0U;k<6U;++k)if(be(b,2U)==magic[k]){index=(int)k;break;}
    if(index<0 || (r->protocol!=WRJ_PROTOCOL_AUTO && (uint32_t)r->protocol!=(uint32_t)index+1U))return 0;
    const uint32_t payload=be(b+4U,2U),minimum=index==5?8U:12U;
    if(b[2]!=(uint8_t)(32U+code[index]) || payload<minimum || payload>4096U || n!=payload+13U)return 0;
    r->protocol=(wrj_protocol_t)(index+1);r->frame_valid=1U;
    r->received_crc=be(b+n-2U,2U);r->computed_crc=wrj_crc16_received(b,n-2U);
    r->crc_valid=(uint8_t)(r->received_crc==r->computed_crc);
    if(!r->crc_valid)return 0;
    const uint8_t *p=b+11U;int semantic=b[6]<=7U;
    if(index==0 || index==3)semantic&=p[0]==0xc2U && p[1]==b[6];
    else if(index==1 || index==4)semantic&=p[0]==0xd0U && p[2]==0U && p[3]==1U &&
        be(p+4U,2U)==payload && p[6]==b[6];
    else if(index==2)semantic&=p[0]==0xd1U;
    else if(payload>=12U)semantic&=p[0]==0x7eU;
    if(payload>=12U){const uint8_t seq=(index==0 || index==3)?p[2]:p[1];
        semantic&=seq==(uint8_t)(b[3]+1U);}
    r->semantic_complete=(uint8_t)semantic;r->original_crc_present=1U;
    field(r,"syncWord",1U,2U,be(b,2U));field(r,"version",3U,3U,2.0);
    field(r,"messageType",3U,3U,code[index]);field(r,"sequenceNumber",4U,4U,b[3]);
    field(r,"payloadLength",5U,6U,payload);field(r,"stateCode",7U,7U,b[6]);
    field(r,"deviceId",8U,9U,be(b+7U,2U));field(r,"timestampCounter",10U,11U,be(b+9U,2U));
    field(r,"payload",12U,11U+payload,NAN);
    if(payload>=12U)field(r,"payloadTag",12U,12U,p[0]);
    if(index==0 || index==3){field(r,"payloadState",13U,13U,p[1]);field(r,"payloadSequence",14U,14U,p[2]);
        field(r,"controlOctets",15U,19U,NAN);}
    else if(index==1 || index==4){field(r,"payloadSequence",13U,13U,p[1]);field(r,"fragmentIndex",14U,14U,p[2]);
        field(r,"fragmentCount",15U,15U,p[3]);field(r,"fragmentLength",16U,17U,be(p+4U,2U));
        field(r,"payloadState",18U,18U,p[6]);field(r,"streamOctet",19U,19U,p[7]);}
    else if(index==2){field(r,"payloadSequence",13U,13U,p[1]);field(r,"latitudeDeg",14U,15U,31.0+be(p+2U,2U)/1e4);
        field(r,"longitudeDeg",16U,17U,121.0+be(p+4U,2U)/1e4);field(r,"identityOctets",18U,21U,NAN);}
    else if(payload>=12U){field(r,"payloadSequence",13U,13U,p[1]);field(r,"payloadState",14U,14U,p[2]);}
    field(r,"engineeringCrc16",n-1U,n,r->received_crc);
    return semantic;
}
wrj_status_t wrj_parse_result_init(wrj_parse_result_t *r)
{
    if(!r)return WRJ_ERR_ARGUMENT;memset(r,0,sizeof(*r));r->capacity=WRJ_RX_MAX_RECORDS;
    r->records=calloc(r->capacity,sizeof(*r->records));return r->records?WRJ_OK:WRJ_ERR_MEMORY;
}
void wrj_parse_result_release(wrj_parse_result_t *r)
{if(r){free(r->records);memset(r,0,sizeof(*r));}}
int wrj_rx_append(wrj_parse_result_t *out,const wrj_rx_record_t *r,uint32_t tolerance)
{
    for(uint32_t k=0U;k<out->count;++k){const wrj_rx_record_t *v=out->records+k;
        if(strcmp(v->candidate,r->candidate)==0 && v->byte_count==r->byte_count &&
            memcmp(v->bytes,r->bytes,r->byte_count)==0 && llabs((int64_t)v->start0-r->start0)<=tolerance)return 0;}
    if(out->count>=out->capacity)return 0;out->records[out->count++]=*r;return 1;
}
static void pack_bits(const uint8_t *bits,uint8_t *bytes,uint32_t n)
{for(uint32_t j=0U;j<n;++j){uint8_t b=0U;for(uint32_t k=0U;k<8U;++k)b=(uint8_t)((b<<1U)|(bits[8U*j+k]&1U));bytes[j]=b;}}
int wrj_scan_bits(const uint8_t *bits,uint32_t n,wrj_protocol_t p,const wrj_rx_record_t *context,wrj_parse_result_t *out)
{
    const uint32_t before=out->count;
    for(uint32_t family=1U;family<=6U;++family){
        if(p!=WRJ_PROTOCOL_AUTO && family!=(uint32_t)p)continue;
        uint32_t word=0U,seen=0U;
        for(uint32_t j=0U;j<n;++j){word=((word<<1U)|bits[j])&0xffffU;
            if(j<15U || word!=magic[family-1U])continue;
            if(seen++>=128U)break;
            const uint32_t start=j-15U;uint8_t header[11];
            if(start+88U>n)continue;pack_bits(bits+start,header,11U);
            if(header[2]!=(uint8_t)(32U+code[family-1U]))continue;
            const uint32_t bytes=13U+be(header+4U,2U),minimum=family==6U?21U:25U;
            if(bytes>WRJ_RX_MAX_BYTES || bytes<minimum)continue;
            ++out->stages.frame_candidates;
            if(start+8U*bytes>n){
                if(out->incomplete_count<96U){wrj_scan_event_t *partial=out->incomplete_events+out->incomplete_count++;
                    partial->bit_start0=start;partial->rotation=context->rotation_rad;memcpy(partial->header,header,11U);}
                continue;}

            wrj_rx_record_t r=*context;r.protocol=(wrj_protocol_t)family;r.bit_start0=start;r.byte_count=bytes;
            pack_bits(bits+start,r.bytes,bytes);++out->stages.crc_checked;
            const int semantic=wrj_protocol_parse(&r);out->stages.crc_pass+=r.crc_valid;
            if(out->event_count<16U){wrj_scan_event_t *event=out->events+out->event_count++;
                event->bit_start0=start;event->rotation=r.rotation_rad;memcpy(event->header,header,11U);}
            out->stages.semantic_parse+=(uint32_t)semantic;
            if(!semantic)continue;
            if(r.active && r.nfft){const int64_t s=(int64_t)r.start0+
                ((int64_t)(start/(2U*r.active))-1)*(r.nfft+r.cp);r.start0=(uint32_t)WRJ_MAX(0,s);}
            (void)wrj_rx_append(out,&r,r.nfft+r.cp);
        }
    }
    return (int)(out->count-before);
}
static uint32_t soft_checks_remaining=32U;
void wrj_soft_data_budget_reset(uint32_t checks){soft_checks_remaining=WRJ_MIN(checks,32U);}
static int compare_confidence(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);}
typedef struct {uint32_t mask,last;double penalty;} chase_node_t;
static int chase_order(const void *a,const void *b)
{const chase_node_t *x=a,*y=b;if(x->penalty!=y->penalty)return(x->penalty>y->penalty)-(x->penalty<y->penalty);
 return(x->mask>y->mask)-(x->mask<y->mask);}
static int soft_data_list(const uint8_t *bits,const double *confidence,uint32_t n,
    const wrj_rx_record_t *context,wrj_parse_result_t *out)
{
    uint32_t events=0U;const clock_t search_begin=clock();
    for(uint32_t family=1U;family<=6U;++family){
        if(context->protocol!=WRJ_PROTOCOL_AUTO && family!=(uint32_t)context->protocol)continue;
        uint32_t word=0U,seen=0U;
        for(uint32_t j=0U;j<n && soft_checks_remaining;++j){word=((word<<1U)|bits[j])&0xffffU;
            if(j<15U || word!=magic[family-1U])continue;if(seen++>=128U)break;
            const uint32_t start=j-15U;uint8_t header[11];if(start+88U>n)continue;pack_bits(bits+start,header,11U);
            const uint32_t bytes=13U+be(header+4U,2U),minimum=family==6U?21U:25U;
            if(header[2]!=32U+code[family-1U] || bytes<minimum || bytes>WRJ_RX_MAX_BYTES || start+8U*bytes>n)continue;
            if(events++>=16U)return 0;if(bytes<25U || header[6]>7U)continue;
            wrj_rx_record_t record=*context;record.protocol=(wrj_protocol_t)family;record.byte_count=bytes;record.bit_start0=start;
            pack_bits(bits+start,record.bytes,bytes);const uint32_t suffix=be(record.bytes+bytes-2U,2U);
            if(wrj_crc16_received(record.bytes,bytes-2U)==suffix)continue;
            uint32_t positions[4]={0U},used=0U;double lowest[4]={0.0};
            const uint32_t data_count=(bytes-13U)*8U;double *ordered=malloc(data_count*sizeof(*ordered));if(!ordered)return 0;
            for(uint32_t bit=0U;bit<data_count;++bit){const uint32_t pos=start+88U+bit;const double v=confidence[pos];ordered[bit]=v;
                if(!isfinite(v)){free(ordered);return 0;}if(v>.20)continue;uint32_t rank=0U;while(rank<used && lowest[rank]<=v)++rank;
                if(rank>=4U)continue;for(uint32_t k=WRJ_MIN(used,3U);k>rank;--k){lowest[k]=lowest[k-1U];positions[k]=positions[k-1U];}
                lowest[rank]=v;positions[rank]=pos;if(used<4U)++used;}
            qsort(ordered,data_count,sizeof(*ordered),compare_confidence);
            const double median=data_count&1U?ordered[data_count/2U]:.5*(ordered[data_count/2U-1U]+ordered[data_count/2U]);free(ordered);
            if(median<.25 || !used)continue;
            chase_node_t frontier[128];uint32_t queued=used;
            for(uint32_t k=0U;k<used;++k)frontier[k]=(chase_node_t){1U<<k,k,lowest[k]};
            while(queued && soft_checks_remaining){
                if((double)(clock()-search_begin)/CLOCKS_PER_SEC>.1)return 0;
                qsort(frontier,queued,sizeof(*frontier),chase_order);chase_node_t node=frontier[0];
                const uint32_t keep=WRJ_MIN(queued-1U,soft_checks_remaining-1U);
                memmove(frontier,frontier+1U,keep*sizeof(*frontier));queued=keep;
                wrj_rx_record_t trial=record;--soft_checks_remaining;trial.soft_bit_count=0U;
                for(uint32_t k=0U;k<used;++k)if(node.mask&(1U<<k)){uint32_t local=positions[k]-start;
                    trial.bytes[local/8U]^=(uint8_t)(1U<<(7U-local%8U));
                    trial.soft_bit_indices[trial.soft_bit_count]=local+1U;trial.soft_bit_confidence[trial.soft_bit_count++]=lowest[k];}
                ++out->stages.crc_checked;
                if(wrj_crc16_received(trial.bytes,bytes-2U)==suffix){
                    const int valid=wrj_protocol_parse(&trial);++out->stages.crc_pass;out->stages.semantic_parse+=(uint32_t)valid;
                    if(valid){
                        if(trial.active && trial.nfft){const int64_t physical=(int64_t)trial.start0+
                            ((int64_t)(start/(2U*trial.active))-1)*(trial.nfft+trial.cp);trial.start0=(uint32_t)WRJ_MAX(0,physical);}
                        return wrj_rx_append(out,&trial,trial.nfft+trial.cp);}
                }
                for(uint32_t next=node.last+1U;next<used;++next){
                    if(queued<128U)frontier[queued++]=(chase_node_t){node.mask+(1U<<next),next,node.penalty+lowest[next]};}
            }
        }
    }
    return 0;
}
static int symbols_to_records_impl(const double complex *symbols,uint32_t count,const wrj_rx_record_t *context,wrj_parse_result_t *out,int allow_soft)
{
    uint8_t *bits=calloc(2U*count,sizeof(uint8_t));if(!bits)return 0;
    /* An exhausted cooperative budget cannot consume soft evidence. Avoid
     * allocating/filling it; the hard demapper and CRC path are unchanged. */
    const int collect_soft=allow_soft && soft_checks_remaining;
    double *confidence=collect_soft?calloc(2U*count,sizeof(*confidence)):NULL;
    if(collect_soft && !confidence){free(bits);return 0;}
    const uint32_t before=out->count;
    for(uint32_t q=0U;q<4U;++q){
        const double complex rot=cexp(-I*q*WRJ_PI/2.0);wrj_rx_record_t r=*context;r.rotation_rad=q*WRJ_PI/2.0;
        for(uint32_t j=0U;j<count;++j){const double complex z=symbols[j]*rot;
            bits[2U*j]=(uint8_t)(creal(z)<0.0);bits[2U*j+1U]=(uint8_t)(cimag(z)<0.0);
            if(confidence){double scale=WRJ_MAX(cabs(z),2.2204460492503131e-16);
                confidence[2U*j]=fabs(creal(z))/scale;confidence[2U*j+1U]=fabs(cimag(z))/scale;}}
        if(wrj_scan_bits(bits,2U*count,context->protocol,&r,out)>0)break;
        if(allow_soft && soft_checks_remaining && soft_data_list(bits,confidence,2U*count,&r,out)>0)break;
        /* Repetitions are soft observations from this same received burst.
         * Admit a combined payload only against its original received CRC. */
        if(context->protocol==WRJ_PROTOCOL_AUTO || context->protocol==WRJ_PROTOCOL_DRONEID){
            uint32_t word=0U,attempts=0U;
            for(uint32_t j=0U;j<2U*count;++j){word=((word<<1U)|bits[j])&0xffffU;
                if(j<15U || word!=0xd10dU)continue;if(attempts++>=32U)break;
                const uint32_t start=j-15U;uint8_t header[11];if(start+88U>2U*count)continue;
                pack_bits(bits+start,header,11U);const uint32_t payload=be(header+4U,2U);
                if(header[2]!=0x23U || payload<96U || payload>180U)continue;
                const uint32_t length=8U*(payload+13U);if(2U*count<2U*length)continue;
                double soft[1544]={0.0};uint8_t combined[1544];
                for(uint32_t k=0U;k<count;++k){const double complex z=symbols[k]*rot;
                    soft[((int64_t)(2U*k)-(int64_t)start+(int64_t)length*(start/length+1U))%length]+=creal(z);
                    soft[((int64_t)(2U*k+1U)-(int64_t)start+(int64_t)length*(start/length+1U))%length]+=cimag(z);}
                for(uint32_t k=0U;k<length;++k)combined[k]=(uint8_t)(soft[k]<0.0);
                wrj_rx_record_t frame=r;frame.byte_count=payload+13U;frame.bit_start0=start;frame.protocol=WRJ_PROTOCOL_DRONEID;
                pack_bits(combined,frame.bytes,frame.byte_count);++out->stages.frame_candidates;++out->stages.crc_checked;
                const int valid=wrj_protocol_parse(&frame);out->stages.crc_pass+=frame.crc_valid;out->stages.semantic_parse+=(uint32_t)valid;
                if(valid){frame.repetition_combined=1U;(void)wrj_rx_append(out,&frame,100U);break;}
            }
            if(out->count>before)break;
        }
    }
    free(confidence);free(bits);return (int)(out->count-before);
}
int wrj_symbols_to_records(const double complex *s,uint32_t n,const wrj_rx_record_t *c,wrj_parse_result_t *out)
{return symbols_to_records_impl(s,n,c,out,0);}
int wrj_symbols_to_records_soft(const double complex *s,uint32_t n,const wrj_rx_record_t *c,wrj_parse_result_t *out)
{return symbols_to_records_impl(s,n,c,out,1);}

int wrj_received_window_end(uint32_t samples,uint32_t current_end0,const wrj_rx_record_t *c,
    const uint8_t header[11],uint32_t bit_start0,uint32_t *end0)
{
    if(!c || !header || !end0)return 0;*end0=current_end0;
    if(!c->nfft || !c->active || c->active>c->nfft || current_end0>samples ||
       current_end0<=c->start0 || bit_start0%(2U*c->active))return 0;
    if(c->byte_count>=11U && memcmp(c->bytes,header,11U))return 0;
    uint32_t family=0U;for(uint32_t k=0U;k<6U;++k)if(be(header,2U)==magic[k]){family=k+1U;break;}
    if(!family || header[2]!=32U+code[family-1U] || header[6]>7U)return 0;
    const uint32_t bytes=13U+be(header+4U,2U),minimum=family==6U?21U:25U;
    if(bytes<minimum || bytes>4109U)return 0;
    const uint64_t symbols=((uint64_t)bit_start0+8U*bytes+2U*c->active-1U)/(2U*c->active);
    const uint64_t wanted=c->start0+(symbols+1U)*(c->nfft+c->cp);
    if(wanted>samples || wanted<=current_end0)return 0;*end0=(uint32_t)wanted;return 1;
}
