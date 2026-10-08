#include "wrj_phy.h"
#include <stdlib.h>
static void movmean(const double complex *x,double complex *out,uint32_t n,uint32_t w)
{
    uint32_t left=0U,right=0U;double complex sum=0.0;
    for(uint32_t k=0U;k<n;++k){const uint32_t a=k>w/2U?k-w/2U:0U,b=WRJ_MIN(n,k+(w-1U)/2U+1U);
        while(right<b)sum+=x[right++];while(left<a)sum-=x[left++];out[k]=sum/(right-left);}
}
static void phase_correct(const double complex *symbols,double complex *corrected,uint32_t n,double complex *scratch)
{
    for(uint32_t k=0U;k<n;++k){const double complex u=symbols[k]/WRJ_MAX(cabs(symbols[k]),2.2204460492503131e-16);scratch[k]=u*u*u*u;}
    movmean(scratch,corrected,n,31U);double previous=0.0,unwrap=0.0;
    for(uint32_t k=0U;k<n;++k){const double angle=carg(corrected[k]);if(k){const double delta=angle-previous;
            if(delta>WRJ_PI)unwrap-=2.0*WRJ_PI;else if(delta<-WRJ_PI)unwrap+=2.0*WRJ_PI;}previous=angle;
        corrected[k]=symbols[k]*cexp(-I*((angle+unwrap)/4.0-WRJ_PI/4.0));}
}
static double complex decision(double complex z)
{return ((creal(z)>0.0?1.0:creal(z)<0.0?-1.0:0.0)+I*(cimag(z)>0.0?1.0:cimag(z)<0.0?-1.0:0.0))/sqrt(2.0);}
static int double_compare(const void *a,const void *b)
{const double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);}
static void refine_symbols(const double complex *symbols,double complex *refined,uint32_t n,double complex *scratch)
{
    for(uint32_t k=0U;k<n;++k)scratch[k]=symbols[k]*conj(decision(symbols[k]));
    movmean(scratch,refined,n,31U);
    double *amplitude=calloc(n,sizeof(double));if(!amplitude){memcpy(refined,symbols,n*sizeof(*symbols));return;}
    for(uint32_t k=0U;k<n;++k){refined[k]=symbols[k]*cexp(-I*carg(refined[k]));amplitude[k]=cabs(refined[k]);}
    qsort(amplitude,n,sizeof(double),double_compare);const double position=WRJ_CLAMP(.3*n-.5,0.0,n-1.0);
    const uint32_t lower=(uint32_t)floor(position);const double threshold=amplitude[lower]+(position-lower)*(amplitude[WRJ_MIN(n-1U,lower+1U)]-amplitude[lower]);free(amplitude);
    double complex gram[9]={0.0},rhs[3]={0.0};uint32_t used=0U;
    for(uint32_t k=0U;k<n;++k){const double complex z=refined[k];const double confidence=WRJ_MIN(fabs(creal(z)),fabs(cimag(z)))/WRJ_MAX(cabs(z),2.2204460492503131e-16);
        if(confidence<=.25 || cabs(z)<=threshold)continue;++used;const double complex d=decision(z),row[3]={d,conj(d),1.0};
        for(uint32_t j=0U;j<3U;++j){rhs[j]+=conj(row[j])*z;for(uint32_t l=0U;l<3U;++l)gram[3U*j+l]+=conj(row[j])*row[l];}}
    if(used<100U || !wrj_solve_complex(gram,rhs,3U) || cabs(rhs[1])>.25*cabs(rhs[0]) || cabs(rhs[0])<2.2204460492503131e-16)return;
    const double gain=pow(cabs(rhs[0]),2.0)-pow(cabs(rhs[1]),2.0);
    for(uint32_t k=0U;k<n;++k){const double complex centered=refined[k]-rhs[2];refined[k]=(conj(rhs[0])*centered-rhs[1]*conj(centered))/gain;}
}
static int header_equalize(const double complex *symbols,double complex *equalized,uint32_t n,const wrj_parse_result_t *rx,double complex *scratch)
{
    for(uint32_t event=0U;event<WRJ_MIN(4U,rx->event_count);++event){const wrj_scan_event_t *e=rx->events+event;
        const uint32_t start=e->bit_start0/2U;if(start+44U>=n)continue;const double complex rot=cexp(-I*e->rotation);
        double complex gram[25]={0.0},rhs[5]={0.0};uint32_t used=0U;
        for(uint32_t sample=2U;sample<=41U;++sample){double complex row[5];for(uint32_t j=0U;j<5U;++j)row[j]=symbols[start+sample+j-2U]*rot;
            const double confidence=WRJ_MIN(fabs(creal(row[2])),fabs(cimag(row[2])))/WRJ_MAX(cabs(row[2]),2.2204460492503131e-16);if(confidence<=.3)continue;++used;
            const uint32_t bit=sample*2U;const double complex target=((1.0-2.0*((e->header[bit/8U]>>(7U-bit%8U))&1U))+I*
                (1.0-2.0*((e->header[(bit+1U)/8U]>>(7U-(bit+1U)%8U))&1U)))/sqrt(2.0);
            for(uint32_t j=0U;j<5U;++j){rhs[j]+=conj(row[j])*target;for(uint32_t l=0U;l<5U;++l)gram[j*5U+l]+=conj(row[j])*row[l];}}
        if(used<24U)continue;double ridge=0.0;for(uint32_t k=0U;k<5U;++k)ridge+=creal(gram[k*5U+k]);ridge*=.03/5.0;
        for(uint32_t k=0U;k<5U;++k)gram[k*5U+k]+=ridge;if(!wrj_solve_complex(gram,rhs,5U))continue;
        double side=0.0;for(uint32_t k=0U;k<5U;++k)if(k!=2U)side+=pow(cabs(rhs[k]),2.0);if(sqrt(side)>cabs(rhs[2]))continue;
        for(uint32_t k=0U;k<n;++k){double complex value=0.0;for(uint32_t j=0U;j<5U;++j){const int64_t pos=(int64_t)k+j-2;if(pos>=0 && pos<n)value+=symbols[pos]*rot*rhs[j];}
            equalized[k]=value;const double confidence=WRJ_MIN(fabs(creal(value)),fabs(cimag(value)))/WRJ_MAX(cabs(value),2.2204460492503131e-16);
            scratch[k]=confidence>.3?value*conj(decision(value)):0.0;}
        double complex *means=calloc(n,sizeof(*means));if(!means)return 0;movmean(scratch,means,n,31U);
        for(uint32_t k=0U;k<n;++k)equalized[k]*=cexp(-I*carg(means[k]))*conj(rot);free(means);return 1;
    }
    memcpy(equalized,symbols,n*sizeof(*symbols));return 0;
}
static int sc_segment(const double complex *x,uint32_t n,double fs,const wrj_rx_record_t *context,wrj_parse_result_t *out)
{
    uint32_t nf=262144U;while(nf<8ULL*n && nf<1048576U)nf<<=1U;
    double complex *fourth=calloc(nf,sizeof(*fourth)),*mixed=calloc(n,sizeof(*mixed)),*averaged=calloc(n,sizeof(*averaged)),*matched=calloc(n,sizeof(*matched));
    double complex *symbols=calloc(n/4U+1U,sizeof(*symbols)),*corrected=calloc(n/4U+1U,sizeof(*corrected)),*scratch=calloc(n/4U+1U,sizeof(*scratch));
    const uint32_t before=out->count;if(!fourth || !mixed || !averaged || !matched || !symbols || !corrected || !scratch)goto finish;
    for(uint32_t k=0U;k<WRJ_MIN(n,nf);++k)fourth[k]=x[k]*x[k]*x[k]*x[k];
    if(wrj_dft_complex(fourth,nf,0)!=WRJ_OK)goto finish;
    uint32_t peak=0U;double maximum=-1.0;
    for(uint32_t k=0U;k<nf;++k){const double power=pow(cabs(fourth[k]),2.0);if(power>maximum){maximum=power;peak=k;}}
    const double l=log(pow(cabs(fourth[(peak+nf-1U)%nf]),2.0)+2.2204460492503131e-16),
        center=log(maximum+2.2204460492503131e-16),r=log(pow(cabs(fourth[(peak+1U)%nf]),2.0)+2.2204460492503131e-16);
    double bin=peak+WRJ_CLAMP(.5*(l-r)/(l-2.0*center+r+2.2204460492503131e-16),-.5,.5);
    if(bin>nf/2.0)bin-=nf;double carrier=2.0*WRJ_PI*bin/nf/4.0;
    uint32_t psd_length=2048U;while(psd_length<n && psd_length<16384U)psd_length<<=1U;
    memset(fourth,0,psd_length*sizeof(*fourth));for(uint32_t k=0U;k<WRJ_MIN(n,psd_length);++k)fourth[k]=x[k];
    (void)wrj_dft_complex(fourth,psd_length,0);maximum=-1.0;peak=0U;
    for(uint32_t k=0U;k<psd_length;++k){const uint32_t a=k>15U?k-15U:0U,b=WRJ_MIN(psd_length,k+16U);double power=0.0;
        for(uint32_t j=a;j<b;++j)power+=pow(cabs(fourth[(j+psd_length/2U)%psd_length]),2.0);
        power/=b-a;if(power>maximum){maximum=power;peak=k;}}
    const double coarse=2.0*WRJ_PI*((double)peak-psd_length/2.0)/psd_length;
    carrier+=round((coarse-carrier)/(WRJ_PI/2.0))*(WRJ_PI/2.0);
    if(getenv("WRJ_SC_STAGE_PROBE"))fprintf(stderr,"SC_STAGE start0=%u count=%u carrier_hz=%.17g\n",context->start0,n,carrier*fs/(2.0*WRJ_PI));
    for(uint32_t k=0U;k<n;++k)mixed[k]=x[k]*cexp(-I*carrier*k);
    for(uint32_t sps=4U;sps<=WRJ_MIN(48U,(uint32_t)ceil(fs/.8e6));++sps){
        movmean(mixed,averaged,n,WRJ_MAX(1U,sps/2U));
        const uint32_t pulse=WRJ_MAX(3U,(uint32_t)lround(.35*sps));
        /* conv(...,'same') uses zero padding and chooses the right centre
         * for even FIR length, unlike movmean's shrinking endpoints. */
        for(uint32_t k=0U;k<n;++k){double complex sum=0.0;
            for(uint32_t j=0U;j<pulse;++j){const int64_t pos=(int64_t)k+pulse/2U-j;if(pos>=0 && pos<n)sum+=mixed[pos];}
            matched[k]=sum/pulse;}
        for(uint32_t timing=0U;timing<sps;++timing){uint32_t count=0U;
            for(uint32_t k=timing;k<n-sps;k+=sps)symbols[count++]=averaged[k];if(count<100U)continue;
            phase_correct(symbols,corrected,count,scratch);wrj_rx_record_t record=*context;
            record.cfo_hz=carrier*fs/(2.0*WRJ_PI);record.nfft=record.cp=record.active=0U;
            const uint32_t old=out->count,headers=out->stages.frame_candidates;
            out->event_count=0U;
            (void)wrj_symbols_to_records(corrected,count,&record,out);
            if(out->count==old && out->event_count){
                memcpy(symbols,corrected,count*sizeof(*symbols));refine_symbols(symbols,corrected,count,scratch);
                (void)wrj_symbols_to_records(corrected,count,&record,out);}
            if(out->count==old && out->stages.frame_candidates>headers){
                for(uint32_t j=0U;j<count;++j)symbols[j]=matched[timing+j*sps];
                phase_correct(symbols,corrected,count,scratch);out->event_count=0U;(void)wrj_symbols_to_records(corrected,count,&record,out);
                if(out->count==old && out->event_count){memcpy(symbols,corrected,count*sizeof(*symbols));
                    (void)header_equalize(symbols,corrected,count,out,scratch);(void)wrj_symbols_to_records(corrected,count,&record,out);}}
            ++out->stages.cfo_success;++out->stages.symbol_success;
            for(uint32_t j=old;j<out->count;++j){const int64_t pos=(int64_t)context->start0+timing+
                (int64_t)(out->records[j].bit_start0/2U)*sps-64U*sps;
                out->records[j].start0=(uint32_t)WRJ_MAX(0,pos);}
            if(out->count>before)goto finish;
        }
    }
finish:free(fourth);free(mixed);free(averaged);free(matched);free(symbols);free(corrected);free(scratch);return (int)(out->count-before);
}
static int order_double(const void *a,const void *b)
{const double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);}
static double prctile(double *v,uint32_t n,double p)
{const double pos=WRJ_CLAMP(p*n-.5,0.0,n-1.0);const uint32_t k=(uint32_t)floor(pos);
return v[k]+(pos-k)*(v[WRJ_MIN(k+1U,n-1U)]-v[k]);}
int wrj_sc_receive(const double complex *x,uint32_t n,double fs,const wrj_rx_record_t *context,wrj_parse_result_t *out)
{
    double *envelope=calloc(n,sizeof(double)),*sorted=calloc(n,sizeof(double));uint8_t *mask=calloc(n,1U),*seeds=calloc(n,1U);
    const uint32_t before=out->count;if(!envelope || !sorted || !mask || !seeds)goto finish;
    uint32_t left=0U,right=0U;double sum=0.0;
    for(uint32_t k=0U;k<n;++k){const uint32_t a=k>32U?k-32U:0U,b=WRJ_MIN(n,k+32U);
        while(right<b){sum+=pow(cabs(x[right]),2.0);++right;}while(left<a){sum-=pow(cabs(x[left]),2.0);++left;}
        envelope[k]=sorted[k]=sum/(right-left);}
    qsort(sorted,n,sizeof(double),order_double);const double floor=prctile(sorted,n,.15),high=prctile(sorted,n,.98),threshold=floor+.25*(high-floor);
    for(uint32_t k=0U;k<n;++k)mask[k]=(uint8_t)(envelope[k]>threshold);
    for(uint32_t k=0U;k<n;){if(!mask[k]){++k;continue;}const uint32_t start=k;while(k<n && mask[k])++k;
        if(k-start>301U)seeds[start]=1U;}
    const uint32_t gap=(uint32_t)lround(32e-6*fs);
    for(uint32_t k=0U;k<n;){if(mask[k]){++k;continue;}const uint32_t start=k;while(k<n && !mask[k])++k;
        if(start>0U && k<n && k-start-1U<gap)memset(mask+start,1,k-start);}
    uint32_t regions=0U;const uint32_t guard=WRJ_MAX(2048U,(uint32_t)lround(64e-6*fs));
    for(uint32_t k=0U;k<n && regions<64U;){if(!mask[k]){++k;continue;}const uint32_t start=k;int seeded=0;
        while(k<n && mask[k]){seeded|=seeds[k];++k;}if(!seeded || k-start<=301U)continue;++regions;
        const uint32_t a=start>guard?start-guard:0U,b=WRJ_MIN(n,k+guard);wrj_rx_record_t record=*context;record.start0=a;
        if(sc_segment(x+a,b-a,fs,&record,out)>0)goto finish;
    }
    (void)sc_segment(x,n,fs,context,out);
finish:free(envelope);free(sorted);free(mask);free(seeds);return (int)(out->count-before);
}
