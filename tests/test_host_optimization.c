#include "wrj_phy.h"
#include <stdlib.h>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"FAIL %d %s\n",__LINE__,#v);return 1;}}while(0)
static void symbols_from_bytes(const uint8_t *bytes,double complex *symbols)
{
    for(uint32_t k=0U;k<100U;++k){uint32_t bit=2U*k;
        double a=((bytes[bit/8U]>>(7U-bit%8U))&1U)?-.7:.7;
        double b=((bytes[(bit+1U)/8U]>>(7U-(bit+1U)%8U))&1U)?-.7:.7;
        symbols[k]=a+I*b;}
}
int main(void)
{
    uint8_t bytes[25]={0xd1U,0xc2U,0x21U,0U,0U,12U,0U,0U,1U,0U,2U,0xc2U,0U,1U};
    uint16_t crc=wrj_crc16_received(bytes,23U);bytes[23]=(uint8_t)(crc>>8U);bytes[24]=(uint8_t)crc;
    wrj_rx_record_t context;memset(&context,0,sizeof(context));context.protocol=WRJ_PROTOCOL_DJI_CONTROL;
    strcpy(context.byte_source,"REAL_IQ");strcpy(context.candidate,"unit");strcpy(context.source,"fixture");
    wrj_parse_result_t rx;CHECK(wrj_parse_result_init(&rx)==WRJ_OK);
    double complex symbols[100];symbols_from_bytes(bytes,symbols);
    symbols[61U]=-(creal(symbols[61U])>0.0?.01:-.01)+I*cimag(symbols[61U]);
    wrj_soft_data_budget_reset(32U);CHECK(wrj_symbols_to_records_soft(symbols,100U,&context,&rx)==1);
    CHECK(rx.records[0].soft_bit_count==1U && rx.records[0].soft_bit_indices[0]==123U);
    CHECK(memcmp(rx.records[0].bytes,bytes,25U)==0 && rx.records[0].received_crc==crc);
    rx.count=0U;memset(&rx.stages,0,sizeof(rx.stages));
    wrj_soft_data_budget_reset(0U);CHECK(wrj_symbols_to_records_soft(symbols,100U,&context,&rx)==0);
    symbols_from_bytes(bytes,symbols);symbols[99U]=creal(symbols[99U])-I*cimag(symbols[99U]);
    wrj_soft_data_budget_reset(32U);CHECK(wrj_symbols_to_records_soft(symbols,100U,&context,&rx)==0);
    /* Reused Bluestein kernel agrees with the first transform and inverse. */
    double complex first[768],repeat[768],input[768];
    for(uint32_t k=0U;k<768U;++k)input[k]=sin(.13*k)+I*cos(.07*k);
    memcpy(first,input,sizeof(first));memcpy(repeat,input,sizeof(repeat));
    CHECK(wrj_dft_complex(first,768U,0)==WRJ_OK && wrj_dft_complex(repeat,768U,0)==WRJ_OK);
    CHECK(memcmp(first,repeat,sizeof(first))==0);CHECK(wrj_dft_complex(first,768U,1)==WRJ_OK);
    for(uint32_t k=0U;k<768U;++k)CHECK(cabs(first[k]-input[k])<1e-10);
    wrj_parse_result_release(&rx);puts("Bounded received-data list, immutable CRC suffix, FFT-plan equivalence passed");return 0;
}
