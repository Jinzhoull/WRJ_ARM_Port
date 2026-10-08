#include "wrj_phy.h"
#include "wrj_ble_e34.h"
#include <stdlib.h>
wrj_status_t wrj_ble_filter_decode_e34_ex(const wrj_cf32_t *iq,uint32_t n,double fs,
    double center,double width,uint32_t out_sps,m3_result_t *out,
    wrj_ble_packet_evidence_t *packets,uint32_t capacity,uint32_t *packet_count,int local_windows)
{
    if(!iq||!out||!n||width<=0.0||!out_sps)return WRJ_ERR_ARGUMENT;
    double complex *spectrum=calloc(n,sizeof(*spectrum));if(!spectrum)return WRJ_ERR_MEMORY;
    for(uint32_t k=0U;k<n;++k)spectrum[k]=((double)iq[k].re+I*iq[k].im)*cexp(-I*2.0*WRJ_PI*center*k/fs);
    wrj_status_t code=wrj_dft_complex(spectrum,n,0);if(code!=WRJ_OK){free(spectrum);return code;}
    for(uint32_t k=0U;k<n;++k){int64_t bin=k<=(n-1U)/2U?(int64_t)k:(int64_t)k-n;double f=bin*fs/n;
        spectrum[k]*=exp(-.5*pow(f/width,8.0));}
    code=wrj_dft_complex(spectrum,n,1);if(code!=WRJ_OK){free(spectrum);return code;}
    uint32_t count=0U,sps=WRJ_MAX(4U,(uint32_t)lround(fs/1e6));
    double complex *resampled=wrj_resample64(spectrum,n,out_sps,sps,&count);free(spectrum);
    if(!resampled)return WRJ_ERR_MEMORY;wrj_cf32_t *actual=calloc(count,sizeof(*actual));
    if(!actual){free(resampled);return WRJ_ERR_MEMORY;}
    for(uint32_t k=0U;k<count;++k){actual[k].re=(float)creal(resampled[k]);actual[k].im=(float)cimag(resampled[k]);}free(resampled);
    code=wrj_ble_decode_e34_ex(actual,count,out_sps*1e6,out,packets,capacity,packet_count);
    if(code==WRJ_OK && local_windows && packets && packet_count){int basic=0,location=0;
        for(uint32_t k=0U;k<out->ble_verified_packet_count;++k){basic|=out->ble_verified_pdu[k][14]>>4U==0U;location|=out->ble_verified_pdu[k][14]>>4U==1U;}
        if(!basic || !location){m3_result_t *local=calloc(1U,sizeof(*local));if(!local){free(actual);return WRJ_ERR_MEMORY;}
            const uint32_t hop=900U*out_sps,span=1500U*out_sps,last_start=count>400U*out_sps?count-400U*out_sps:0U;
            for(uint32_t window=0U,first=0U;window<7U && first<=last_start && *packet_count<capacity;++window,first+=hop){
                uint32_t extra=0U;code=wrj_ble_decode_e34_ex(actual+first,WRJ_MIN(span,count-first),out_sps*1e6,local,
                    packets+*packet_count,capacity-*packet_count,&extra);
                if(code!=WRJ_OK)break;
                for(uint32_t k=0U;k<extra;++k)packets[*packet_count+k].start+=first;
                *packet_count+=extra;
            }
            free(local);
        }
    }
    free(actual);
    for(uint32_t k=0U;k<out->ble_verified_packet_count;++k)out->ble_verified_start[k]=(uint32_t)lround(out->ble_verified_start[k]*(double)sps/out_sps);
    if(packets && packet_count)for(uint32_t k=0U;k<*packet_count;++k){
        double start=packets[k].start*(double)sps/out_sps;
        packets[k].start=(uint32_t)lround(start);packets[k].timing=packets[k].timing*sps/out_sps+start-packets[k].start;
    }
    return code;
}
wrj_status_t wrj_ble_filter_decode_e34(const wrj_cf32_t *iq,uint32_t n,double fs,
    double center,double width,uint32_t out_sps,m3_result_t *out)
{
    return wrj_ble_filter_decode_e34_ex(iq,n,fs,center,width,out_sps,out,NULL,0U,NULL,0);
}
