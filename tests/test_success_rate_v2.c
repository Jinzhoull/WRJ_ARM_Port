#include "wrj_phy.h"
#include <stdlib.h>
#define CHECK(v) do{if(!(v)){fprintf(stderr,"V2 FAIL %d %s\n",__LINE__,#v);return 1;}}while(0)
static void constellation(const uint8_t *bytes,double complex *symbols)
{
    for(uint32_t k=0U;k<100U;++k){const uint32_t bit=2U*k;
        const double a=((bytes[bit/8U]>>(7U-bit%8U))&1U)?-.7:.7;
        const double b=((bytes[(bit+1U)/8U]>>(7U-(bit+1U)%8U))&1U)?-.7:.7;
        symbols[k]=a+I*b;}
}
int main(void)
{
    uint8_t bytes[25]={0xd1U,0xc2U,0x21U,0U,0U,12U,0U,0U,1U,0U,2U,0xc2U,0U,1U};
    const uint16_t crc=wrj_crc16_received(bytes,23U);bytes[23]=(uint8_t)(crc>>8U);bytes[24]=(uint8_t)crc;
    wrj_rx_record_t context;memset(&context,0,sizeof(context));context.protocol=WRJ_PROTOCOL_DJI_CONTROL;
    strcpy(context.candidate,"v2_fixture");strcpy(context.source,"fixture");strcpy(context.byte_source,"REAL_IQ");
    wrj_parse_result_t rx;CHECK(wrj_parse_result_init(&rx)==WRJ_OK);double complex symbols[100];
    constellation(bytes,symbols);
    /* Two actual low-margin DATA decisions require a two-bit Chase node. */
    for(uint32_t k=61U;k<=63U;k+=2U)symbols[k]=-(creal(symbols[k])>0.0?.01:-.01)+I*cimag(symbols[k]);
    wrj_soft_data_budget_reset(32U);CHECK(wrj_symbols_to_records_soft(symbols,100U,&context,&rx)==1);
    CHECK(rx.records[0].soft_bit_count==2U && memcmp(rx.records[0].bytes,bytes,25U)==0);
    CHECK(memcmp(rx.records[0].bytes,bytes,11U)==0 && rx.records[0].received_crc==crc);
    rx.count=0U;memset(&rx.stages,0,sizeof(rx.stages));wrj_soft_data_budget_reset(0U);
    CHECK(wrj_symbols_to_records_soft(symbols,100U,&context,&rx)==0);
    constellation(bytes,symbols);symbols[99U]=creal(symbols[99U])-I*cimag(symbols[99U]);
    wrj_soft_data_budget_reset(32U);CHECK(wrj_symbols_to_records_soft(symbols,100U,&context,&rx)==0);
    constellation(bytes,symbols);symbols[12U]=-creal(symbols[12U])+I*cimag(symbols[12U]);
    wrj_soft_data_budget_reset(32U);CHECK(wrj_symbols_to_records_soft(symbols,100U,&context,&rx)==0);
    /* Header-guided extension is in one crop, using a received identity. */
    context.start0=10U;context.nfft=64U;context.cp=16U;context.active=32U;
    context.byte_count=11U;memcpy(context.bytes,bytes,11U);uint32_t end=250U;
    CHECK(wrj_received_window_end(10000U,250U,&context,bytes,64U,&end)==1 && end==490U);
    CHECK(wrj_received_window_end(300U,250U,&context,bytes,64U,&end)==0 && end==250U);
    uint8_t foreign[11];memcpy(foreign,bytes,11U);foreign[3]=1U;
    CHECK(wrj_received_window_end(10000U,250U,&context,foreign,64U,&end)==0);
    CHECK(wrj_received_window_end(10000U,250U,&context,bytes,65U,&end)==0);
    wrj_parse_result_release(&rx);puts("V2 Chase budget, immutable header/CRC, crop/identity/alignment checks PASS");return 0;
}
