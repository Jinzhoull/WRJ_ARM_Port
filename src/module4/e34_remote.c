#include "wrj_phy.h"
#include <stdlib.h>
#include <time.h>
#include "wrj_ble_e34.h"
static uint32_t little(const uint8_t *b,uint32_t n)
{uint32_t v=0U;for(uint32_t k=0U;k<n;++k)v|=(uint32_t)b[k]<<(8U*k);return v;}
static double signed_le(const uint8_t *b)
{const uint32_t v=little(b,4U);return v>=0x80000000U?(double)v-4294967296.0:v;}
static void addfield(wrj_rx_record_t *r,const char *name,uint32_t a,uint32_t b,double value)
{
    if(r->field_count>=WRJ_RX_MAX_FIELDS)return;wrj_rx_field_t *f=&r->fields[r->field_count++];
    memset(f,0,sizeof(*f));
    snprintf(f->semantic,sizeof(f->semantic),"%s",name);f->start_byte=(uint16_t)a;
    f->end_byte=(uint16_t)b;f->numeric_value=value;
}
static int printable(const uint8_t *p)
{uint32_t used=0U;for(uint32_t k=0U;k<20U && p[k];++k){if(p[k]<32U || p[k]>126U)return 0;++used;}return used>0U;}
static int packet_fields(wrj_rx_record_t *r)
{
    const uint8_t *m=r->bytes+14U;const uint8_t type=m[0]>>4U;r->message_type=type;r->field_count=0U;
    if((m[0]&15U)!=2U || (type!=0U && type!=1U && type!=4U && type!=5U))return 0;
    addfield(r,"MessageTypeAndVersion",1U,1U,NAN);
    if(type==0U){addfield(r,"IDTypeAndUAType",2U,2U,NAN);addfield(r,"UAS_ID",3U,22U,NAN);addfield(r,"Reserved",23U,25U,NAN);
        r->semantic_complete=(uint8_t)((m[1]>>4U==1U || m[1]>>4U==2U) && printable(m+2U));}
    else if(type==1U){
        const double heading=m[2]+180.0*((m[1]>>1U)&1U),speed=(m[1]&1U)?63.75+.75*m[3]:.25*m[3];
        const double latitude=signed_le(m+5U)/1e7,longitude=signed_le(m+9U)/1e7;
        addfield(r,"StatusAndFlags",2U,2U,NAN);addfield(r,"Heading",3U,3U,heading>360.0?NAN:fmod(heading,360.0));
        addfield(r,"Speed",4U,4U,speed>254.25?NAN:speed);double vertical=(m[4]>=128U?(double)m[4]-256.0:m[4])*.5;
        addfield(r,"VerticalSpeed",5U,5U,fabs(vertical)>62.0?NAN:vertical);addfield(r,"Latitude",6U,9U,latitude);addfield(r,"Longitude",10U,13U,longitude);
        addfield(r,"PressureAltitude",14U,15U,little(m+13U,2U)?little(m+13U,2U)*.5-1000.0:NAN);
        addfield(r,"Altitude",16U,17U,little(m+15U,2U)?little(m+15U,2U)*.5-1000.0:NAN);
        addfield(r,"Height",18U,19U,little(m+17U,2U)?little(m+17U,2U)*.5-1000.0:NAN);
        addfield(r,"PositionAccuracy",20U,20U,NAN);addfield(r,"SpeedAndBaroAccuracy",21U,21U,NAN);
        addfield(r,"Timestamp",22U,23U,little(m+21U,2U)==65535U?NAN:little(m+21U,2U)/10.0);
        addfield(r,"TimestampAccuracy",24U,24U,NAN);addfield(r,"Reserved",25U,25U,NAN);
        r->semantic_complete=(uint8_t)(heading<=360.0 && speed<=254.25 && little(m+15U,2U)>0U && fabs(latitude)<=90.0 && fabs(longitude)<=180.0);
    }else if(type==4U){
        addfield(r,"OperatorAndClassification",2U,2U,NAN);addfield(r,"OperatorLatitude",3U,6U,signed_le(m+2U)/1e7);
        addfield(r,"OperatorLongitude",7U,10U,signed_le(m+6U)/1e7);addfield(r,"AreaCount",11U,12U,little(m+10U,2U));
        addfield(r,"AreaRadius",13U,13U,m[12]*10.0);addfield(r,"AreaCeiling",14U,15U,little(m+13U,2U)?little(m+13U,2U)*.5-1000.0:NAN);
        addfield(r,"AreaFloor",16U,17U,little(m+15U,2U)?little(m+15U,2U)*.5-1000.0:NAN);
        addfield(r,"CategoryAndClass",18U,18U,NAN);addfield(r,"OperatorAltitude",19U,20U,little(m+18U,2U)?little(m+18U,2U)*.5-1000.0:NAN);
        addfield(r,"SystemTimestamp",21U,24U,little(m+20U,4U));addfield(r,"Reserved",25U,25U,NAN);r->semantic_complete=1U;
    }else {addfield(r,"OperatorIDType",2U,2U,m[1]);addfield(r,"OperatorID",3U,22U,NAN);addfield(r,"Reserved",23U,25U,NAN);r->semantic_complete=(uint8_t)printable(m+2U);}
    const uint32_t definition_count=r->field_count;
    addfield(r,"MessageType",1U,1U,type);addfield(r,"ProtocolVersion",1U,1U,m[0]&15U);
    if(type==0U){addfield(r,"IDType",2U,2U,m[1]>>4U);addfield(r,"UAType",2U,2U,m[1]&15U);}
    else if(type==1U){addfield(r,"Status",2U,2U,m[1]>>4U);addfield(r,"HeightReference",2U,2U,(m[1]>>2U)&1U);
        addfield(r,"EastWestFlag",2U,2U,(m[1]>>1U)&1U);addfield(r,"SpeedMultiplier",2U,2U,m[1]&1U);
        addfield(r,"HorizontalAccuracy",20U,20U,m[19]&15U);addfield(r,"VerticalAccuracy",20U,20U,m[19]>>4U);
        addfield(r,"SpeedAccuracy",21U,21U,m[20]&15U);addfield(r,"BaroAccuracy",21U,21U,m[20]>>4U);
        addfield(r,"TimestampAccuracyCode",24U,24U,m[23]&15U);
    }else if(type==4U){addfield(r,"OperatorLocationType",2U,2U,m[1]&3U);addfield(r,"ClassificationType",2U,2U,(m[1]>>2U)&7U);
        addfield(r,"CategoryEU",18U,18U,m[17]>>4U);addfield(r,"ClassEU",18U,18U,m[17]&15U);}
    for(uint32_t k=0U;k<r->field_count;++k){wrj_rx_field_t *f=r->fields+k;const char *kind="bits";
        if(k>=definition_count)kind="packed_bits";
        else if(strcmp(f->semantic,"Reserved")==0)kind="bytes";
        else if(strcmp(f->semantic,"UAS_ID")==0)kind=(m[1]>>4U==1U || m[1]>>4U==2U)?"ascii":"bytes";
        else if(strcmp(f->semantic,"OperatorID")==0)kind="ascii";
        else if(strstr(f->semantic,"Latitude") || strstr(f->semantic,"Longitude"))kind="latlon";
        else if(strstr(f->semantic,"Altitude") || strcmp(f->semantic,"Height")==0 ||
            strcmp(f->semantic,"AreaCeiling")==0 || strcmp(f->semantic,"AreaFloor")==0)kind="altitude";
        else if(strcmp(f->semantic,"Heading")==0)kind="heading";
        else if(strcmp(f->semantic,"Speed")==0)kind="speed";
        else if(strcmp(f->semantic,"VerticalSpeed")==0)kind="vertical";
        else if(strcmp(f->semantic,"Timestamp")==0)kind="timestamp";
        else if(strcmp(f->semantic,"AreaRadius")==0)kind="radius";
        else if(strcmp(f->semantic,"AreaCount")==0 || strcmp(f->semantic,"SystemTimestamp")==0 ||
            strcmp(f->semantic,"OperatorIDType")==0)kind="uint";
        snprintf(f->data_type,sizeof(f->data_type),"%s",kind);
        snprintf(f->validation_status,sizeof(f->validation_status),"%s",
            isnan(f->numeric_value) && (strcmp(kind,"altitude")==0 || strcmp(kind,"heading")==0 ||
                strcmp(kind,"speed")==0 || strcmp(kind,"vertical")==0 || strcmp(kind,"timestamp")==0)?"UNAVAILABLE":"PASS_CRC24");
        if(strcmp(kind,"ascii")==0){uint32_t used=0U;for(uint32_t j=f->start_byte-1U;j<f->end_byte && m[j] && used+1U<sizeof(f->ascii_value);++j)
            f->ascii_value[used++]=(char)m[j];f->ascii_value[used]='\0';}
    }
    return 1;
}
int wrj_remote_revalidate(wrj_rx_record_t *r)
{
    if(r->byte_count!=39U || r->bytes[1]!=37U || r->bytes[8]!=30U || r->bytes[9]!=0x16U ||
        r->bytes[10]!=0xfaU || r->bytes[11]!=0xffU || r->bytes[12]!=13U)return 0;
    uint32_t received=0U;
    for(uint32_t k=0U;k<24U;++k){if(r->received_crc_bits[k]>1U || r->raw_bits[312U+k]!=r->received_crc_bits[k])return 0;
        received=(received<<1U)|r->received_crc_bits[k];}
    r->computed_crc=wrj_crc24_received(r->bytes,39U);r->received_crc=received;
    r->crc_valid=(uint8_t)(r->computed_crc==received);
    return r->crc_valid && packet_fields(r);
}
int wrj_remote_associate(wrj_parse_result_t *out,double fs)
{
    uint32_t targets=0U;out->complete=0U;snprintf(out->association_mode,sizeof(out->association_mode),"NONE");
    for(uint32_t k=0U;k<out->count;++k){const wrj_rx_record_t *basic=out->records+k;
        if(basic->message_type!=0U || !basic->semantic_complete)continue;int repeated=0,location=0,conflict=0;
        for(uint32_t j=0U;j<k;++j)if(out->records[j].message_type==0U && out->records[j].tx_add==basic->tx_add &&
            memcmp(out->records[j].adv_a,basic->adv_a,6U)==0)repeated=1;
        if(repeated)continue;
        for(uint32_t j=0U;j<out->count;++j){const wrj_rx_record_t *p=out->records+j;
            if(p->tx_add!=basic->tx_add || memcmp(p->adv_a,basic->adv_a,6U)!=0)continue;
            location|=p->message_type==1U && p->semantic_complete;
            if(p->message_type==0U && p->semantic_complete &&
                (p->bytes[15]!=basic->bytes[15] || memcmp(p->bytes+16U,basic->bytes+16U,20U)!=0))conflict=1;
        }
        targets+=(uint32_t)(location && !conflict);
    }
    if(targets==1U){out->complete=1U;snprintf(out->association_mode,sizeof(out->association_mode),"SAME_ADVA");return 1;}
    if(out->count<2U || fs<=0.0)return 0;
    const wrj_rx_record_t *origin=out->records;const uint8_t *identity=NULL,*operator_id=NULL;
    uint32_t earliest=UINT32_MAX,latest=0U,addresses=0U,locations[WRJ_RX_MAX_RECORDS],location_count=0U;
    for(uint32_t k=0U;k<out->count;++k){const wrj_rx_record_t *r=out->records+k;
        if(strcmp(r->candidate,origin->candidate)!=0 || strcmp(r->source,origin->source)!=0 || r->tx_add!=origin->tx_add ||
            !r->crc_valid || !r->original_crc_present)return 0;
        earliest=WRJ_MIN(earliest,r->start0);latest=WRJ_MAX(latest,r->start0);int seen=0;
        for(uint32_t j=0U;j<k;++j)if(memcmp(out->records[j].adv_a,r->adv_a,6U)==0)seen=1;
        addresses+=(uint32_t)!seen;const uint8_t *m=r->bytes+14U;
        if(r->message_type==0U && (m[1]>>4U==1U || m[1]>>4U==2U) && printable(m+2U)){
            if(identity && (identity[1]>>4U!=m[1]>>4U || memcmp(identity+2U,m+2U,20U)!=0))return 0;identity=m;
        }else if(r->message_type==5U){if(!printable(m+2U) || (operator_id && memcmp(operator_id+1U,m+1U,21U)!=0))return 0;operator_id=m;}
        else if(r->message_type==1U)locations[location_count++]=k;
    }
    if(addresses<2U || !identity || !location_count || (latest-earliest)/fs>.25)return 0;
    for(uint32_t k=1U;k<location_count;++k){uint32_t value=locations[k],j=k;
        while(j && out->records[locations[j-1U]].start0>out->records[value].start0){locations[j]=locations[j-1U];--j;}locations[j]=value;}
    double previous_lat=0.0,previous_lon=0.0,previous_stamp=0.0;uint32_t previous_start=0U;
    for(uint32_t k=0U;k<location_count;++k){const wrj_rx_record_t *r=out->records+locations[k];
        uint32_t basic_start=0U;int preceding=0;
        for(uint32_t j=0U;j<out->count;++j)if(out->records[j].message_type==0U && out->records[j].start0<r->start0){
            basic_start=WRJ_MAX(basic_start,out->records[j].start0);preceding=1;}
        if(!preceding || (r->start0-basic_start)/fs>.05)return 0;
        const uint8_t *m=r->bytes+14U;const double latitude=signed_le(m+5U)/1e7,longitude=signed_le(m+9U)/1e7,stamp=little(m+21U,2U)*.1;
        if(fabs(latitude)>90.0 || fabs(longitude)>180.0 || little(m+15U,2U)==0U)return 0;
        if(k){const double distance=111320.0*hypot(latitude-previous_lat,(longitude-previous_lon)*cos((latitude+previous_lat)*WRJ_PI/360.0));
            if(distance>25.0+255.0*(r->start0-previous_start)/fs || (stamp<previous_stamp && previous_stamp-stamp<3500.0))return 0;}
        previous_lat=latitude;previous_lon=longitude;previous_stamp=stamp;previous_start=r->start0;
    }
    out->complete=1U;snprintf(out->association_mode,sizeof(out->association_mode),"LEGACY_E34_ROTATING_ADVA");return 1;
}
int wrj_remote_receive(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,wrj_parse_result_t *out,int deep)
{
    (void)deep;const m3_result_t *cache=&sync->sync;
    if(strcmp(cache->ble_origin_candidate,candidate->candidate_id)!=0 || strcmp(cache->ble_origin_source,candidate->source_file)!=0)return 0;
    for(uint32_t k=0U;k<cache->ble_verified_packet_count;++k){const uint8_t *p=cache->ble_verified_pdu[k];
        ++out->stages.crc_checked;const uint32_t crc=wrj_crc24_received(p,39U);
        if(p[1]!=37U || crc!=cache->ble_verified_crc[k] || p[8]!=30U || p[9]!=0x16U || p[10]!=0xfaU || p[11]!=0xffU || p[12]!=0x0dU)continue;
        uint32_t received=0U;for(uint32_t j=0U;j<24U;++j)received=(received<<1U)|cache->ble_verified_raw_bits[k][312U+j];
        if(received!=cache->ble_verified_crc[k])continue;
        wrj_rx_record_t r;memset(&r,0,sizeof(r));r.protocol=WRJ_PROTOCOL_REMOTEID;r.byte_count=39U;r.start0=cache->ble_verified_start[k];
        snprintf(r.candidate,sizeof(r.candidate),"%s",cache->ble_origin_candidate);snprintf(r.source,sizeof(r.source),"%s",cache->ble_origin_source);
        snprintf(r.byte_source,sizeof(r.byte_source),"REAL_IQ");memcpy(r.bytes,p,39U);r.tx_add=(p[0]>>6U)&1U;memcpy(r.adv_a,p+2U,6U);
        r.received_crc=received;r.computed_crc=crc;r.original_crc_present=r.frame_valid=r.crc_valid=1U;
        memcpy(r.raw_bits,cache->ble_verified_raw_bits[k],336U);memcpy(r.received_crc_bits,r.raw_bits+312U,24U);
        if(!packet_fields(&r))continue;++out->stages.crc_pass;out->stages.semantic_parse+=r.semantic_complete;
        (void)wrj_rx_append(out,&r,UINT32_MAX);
    }
    return wrj_remote_associate(out,sync->fs);
}
static const uint8_t *basic_identity(const wrj_parse_result_t *result)
{
    for(uint32_t k=0U;k<result->count;++k)if(result->records[k].message_type==0U &&
        result->records[k].semantic_complete)return result->records[k].bytes+16U;
    return NULL;
}
static int all_messages(const wrj_parse_result_t *result)
{
    uint32_t types=0U;for(uint32_t k=0U;k<result->count;++k)types|=1U<<result->records[k].message_type;
    return (types&0x33U)==0x33U;
}
wrj_status_t wrj_remote_complete(const wrj_candidate_t *candidate,const wrj_sync_result_t *sync,
    wrj_parse_result_t *out,uint32_t *attempts)
{
    if(!candidate || !sync || !out || !attempts)return WRJ_ERR_ARGUMENT;*attempts=0U;
    if(!out->complete || !sync->compensated_iq || all_messages(out))return WRJ_OK;
    const uint8_t *identity=basic_identity(out);if(!identity)return WRJ_OK;
    uint8_t uas_id[20];memcpy(uas_id,identity,20U);const clock_t begin=clock();
    static const double widths[2]={.9e6,.65e6};static const uint32_t rates[2]={16U,4U};
    wrj_sync_result_t *local=calloc(1U,sizeof(*local));wrj_parse_result_t pool;memset(&pool,0,sizeof(pool));
    if(!local)return WRJ_ERR_MEMORY;wrj_status_t code=wrj_parse_result_init(&pool);if(code!=WRJ_OK){free(local);return code;}
    local->fs=sync->fs;
    snprintf(local->sync.ble_origin_candidate,sizeof(local->sync.ble_origin_candidate),"%s",candidate->candidate_id);
    snprintf(local->sync.ble_origin_source,sizeof(local->sync.ble_origin_source),"%s",candidate->source_file);
    for(uint32_t width=0U;width<2U && !all_messages(out);++width)for(uint32_t rate=0U;rate<2U && !all_messages(out);++rate){
        if((double)(clock()-begin)/CLOCKS_PER_SEC>=8.0)goto finish;
        code=wrj_ble_filter_decode_e34(sync->compensated_iq,sync->sample_count,sync->fs,0.0,widths[width],rates[rate],&local->sync);
        if(code!=WRJ_OK)goto finish;++*attempts;
        pool.count=out->count;memcpy(pool.records,out->records,out->count*sizeof(*out->records));
        (void)wrj_remote_receive(candidate,local,&pool,0);
        const uint8_t *next_identity=basic_identity(&pool);
        if(pool.complete && pool.count>=out->count && next_identity && memcmp(next_identity,uas_id,20U)==0){
            out->count=pool.count;memcpy(out->records,pool.records,pool.count*sizeof(*pool.records));
            (void)wrj_remote_associate(out,sync->fs);
        }
    }
finish:wrj_parse_result_release(&pool);free(local);return code;
}
