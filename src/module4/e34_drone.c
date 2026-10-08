#include "wrj_phy.h"
#include <stdlib.h>
static double complex pilot(uint32_t which,uint32_t k,int legacy)
{static const uint32_t roots[2]={600U,147U};return cexp(-I*(legacy?(double)(which+1U):WRJ_PI)*roots[which]*k*(k+1.0)/601.0);}
static int peak_order(const void *a,const void *b)
{const wrj_phy_peak_t *x=a,*y=b;return(x->score<y->score)-(x->score>y->score);}
static int double_order(const void *a,const void *b)
{const double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);}
static double median(double *values,uint32_t n)
{qsort(values,n,sizeof(double),double_order);return n&1U?values[n/2U]:.5*(values[n/2U-1U]+values[n/2U]);}
static void smooth(double complex *channel,uint32_t width)
{
    double complex sum=0.0,flat[601],out[601];for(uint32_t k=1U;k<601U;++k)sum+=channel[k]*conj(channel[k-1U]);
    const double linear=carg(sum);
    for(uint32_t k=0U;k<601U;++k)flat[k]=channel[k]*cexp(-I*linear*k);
    for(uint32_t k=0U;k<601U;++k){const uint32_t a=k>width/2U?k-width/2U:0U,b=WRJ_MIN(601U,k+width/2U+1U);sum=0.0;
        for(uint32_t j=a;j<b;++j)sum+=flat[j];out[k]=sum/(b-a)*cexp(I*linear*k);}
    memcpy(channel,out,sizeof(out));
}
int wrj_drone_looks(const double complex *x,uint32_t n,double fs)
{
    uint32_t count=0U;double complex *y=wrj_resample64(x,n,15360000U,(uint32_t)lround(fs),&count);
    if(!y || count<1100U){free(y);return 0;}
    double complex spectrum[8192]={0.0};memcpy(spectrum,y,WRJ_MIN(count,8192U)*sizeof(*y));
    (void)wrj_dft_complex(spectrum,8192U,0);double power[8192],sum=0.0,maximum=-1.0;uint32_t peak=0U,a=0U,b=0U;
    for(uint32_t k=0U;k<8192U;++k)power[k]=pow(cabs(spectrum[(k+4096U)%8192U]),2.0);
    for(uint32_t k=0U;k<8192U;++k){uint32_t left=k>1500U?k-1500U:0U,right=WRJ_MIN(8192U,k+1500U);
        while(b<right)sum+=power[b++];while(a<left)sum-=power[a++];double value=sum/(b-a);if(value>maximum){maximum=value;peak=k;}}
    double frequency=((double)peak-4096.0)/8192.0;for(uint32_t k=0U;k<count;++k)y[k]*=cexp(-I*2.0*WRJ_PI*frequency*k);
    wrj_phy_peak_t peaks[64];uint32_t used=wrj_phy_cp_peaks(y,count,1024U,72U,peaks,64U,.4,0);int present=0;
    for(uint32_t h=0U;h<used && !present;++h){uint32_t start=peaks[h].start;if(start+1096U>count)continue;
        double complex body[1024];for(uint32_t k=0U;k<1024U;++k)body[k]=y[start+72U+k]*cexp(-I*peaks[h].phase*k/1024.0);
        (void)wrj_dft_complex(body,1024U,0);for(uint32_t k=0U;k<1024U;++k)spectrum[k]=body[(k+512U)%1024U]/32.0;
        double best=-1.0;uint32_t first=0U;sum=0.0;for(uint32_t k=0U;k<601U;++k)sum+=pow(cabs(spectrum[k]),2.0);
        for(uint32_t k=0U;k+601U<=1024U;++k){if(sum>best){best=sum;first=k;}if(k+601U<1024U)sum+=pow(cabs(spectrum[k+601U]),2.0)-pow(cabs(spectrum[k]),2.0);}
        for(uint32_t legacy=0U;legacy<2U && !present;++legacy)for(uint32_t which=0U;which<2U && !present;++which){
            double complex channel[601],flat[601];sum=0.0;double complex cross=0.0;
            for(uint32_t k=0U;k<601U;++k){channel[k]=spectrum[first+k]/pilot(which,k,(int)legacy);if(k)cross+=channel[k]*conj(channel[k-1U]);}
            double linear=carg(cross),residual=0.0,total=0.0;
            for(uint32_t k=0U;k<601U;++k)flat[k]=channel[k]*cexp(-I*linear*k);
            for(uint32_t k=0U;k<601U;++k){double complex mean=0.0;uint32_t left=k>4U?k-4U:0U,right=WRJ_MIN(601U,k+5U);
                for(uint32_t j=left;j<right;++j)mean+=flat[j];mean/=right-left;residual+=pow(cabs(flat[k]-mean),2.0);total+=pow(cabs(flat[k]),2.0);}
            present=sqrt(WRJ_MAX(0.0,1.0-residual/(total+2.2204460492503131e-16)))>.8;
        }
    }
    free(y);return present;
}
static int decode_family(const double complex *x,uint32_t n,double fs,const wrj_rx_record_t *context,wrj_parse_result_t *out,int legacy)
{
    static const uint32_t cp[9]={80U,72U,72U,72U,72U,72U,72U,72U,80U};
    const uint32_t before=out->count;wrj_phy_peak_t cp_peaks[64];double phases[64];
    const uint32_t num=wrj_phy_cp_peaks(x,n,1024U,72U,cp_peaks,64U,.55,0);
    for(uint32_t k=0U;k<num;++k)phases[k]=cp_peaks[k].phase;
    const double hz=num?median(phases,num)*fs/(2.0*WRJ_PI*1024.0):0.0;
    double complex template[1096],body[1024]={0.0};
    for(uint32_t k=0U;k<601U;++k)body[(212U+k+512U)%1024U]=pilot(0U,k,legacy);
    (void)wrj_dft_complex(body,1024U,1);for(uint32_t k=0U;k<1024U;++k)body[k]*=32.0;
    for(uint32_t k=0U;k<72U;++k)template[k]=body[952U+k];memcpy(template+72U,body,sizeof(body));
    double complex *mixed=calloc(n,sizeof(*mixed));if(!mixed)return 0;
    for(uint32_t k=0U;k<n;++k)mixed[k]=x[k]*cexp(-I*2.0*WRJ_PI*hz*k/fs);
    double *metric=wrj_matched_metric(mixed,n,template,1096U);free(mixed);if(!metric)return 0;
    const uint32_t length=n>=1096U?n-1096U+1U:0U;
    wrj_phy_peak_t *all=calloc(length,sizeof(*all)),selected[64];uint32_t used=0U,count=0U;
    if(!all){free(metric);return 0;}const uint32_t separation=WRJ_MIN(4384U,length>2U?length-2U:1U);
    for(uint32_t k=1U;k+1U<length;++k)if(metric[k]>=.22 && metric[k]>metric[k-1U] && metric[k]>=metric[k+1U])all[used++]=(wrj_phy_peak_t){k,metric[k],0.0};
    qsort(all,used,sizeof(*all),peak_order);
    for(uint32_t k=0U;k<used && count<64U;++k){int separate=1;
        for(uint32_t j=0U;j<count;++j)if(llabs((int64_t)all[k].start-selected[j].start)<=separation)separate=0;
        if(separate)selected[count++]=all[k];}
    out->stages.cp_peaks+=count;free(all);free(metric);
    double complex blocks[9][1024],channels[2][601],channel[601],symbols[4200];uint8_t present[9];
    for(uint32_t candidate=0U;candidate<count;++candidate)for(uint32_t timing=0U;timing<5U;++timing){
        static const int adjustment[5]={0,-2,2,-8,-16};const int64_t trial=(int64_t)selected[candidate].start-3296+adjustment[timing];
        if(trial<0 || trial+4392>n)continue;const uint32_t start=(uint32_t)trial;double complex cross=0.0;
        for(uint32_t which=0U;which<2U;++which){const uint32_t p=which?5U:3U;uint32_t pos=start;
            for(uint32_t k=0U;k<p;++k)pos+=1024U+cp[k];
            if(pos+72U+1024U>n)continue;for(uint32_t k=16U;k<72U;++k)cross+=x[pos+k]*conj(x[pos+k+1024U]);}
        double frequency=hz;if(cabs(cross)>2.2204460492503131e-16){const double local=-carg(cross)*fs/(2.0*WRJ_PI*1024.0);frequency=local+round((hz-local)/(fs/1024.0))*(fs/1024.0);}
        memset(present,0,sizeof(present));uint32_t pos=start;
        for(uint32_t k=0U;k<9U;++k){if(pos+cp[k]+1024U>n)break;
            for(uint32_t j=0U;j<1024U;++j)body[j]=x[pos+cp[k]+j]*cexp(-I*2.0*WRJ_PI*frequency*(pos+cp[k]+j)/fs);
            (void)wrj_dft_complex(body,1024U,0);for(uint32_t j=0U;j<1024U;++j)blocks[k][j]=body[(j+512U)%1024U]/32.0;
            present[k]=1U;pos+=1024U+cp[k];}
        if(!present[3])continue;const uint32_t pilots=present[5]?2U:1U;
        for(uint32_t k=0U;k<601U;++k){channels[0][k]=blocks[3][212U+k]/pilot(0U,k,legacy);
            if(pilots==2U)channels[1][k]=blocks[5][212U+k]/pilot(1U,k,legacy);}
        ++out->stages.sync_success;++out->stages.cfo_success;
        for(uint32_t choice=0U;choice<pilots;++choice)for(uint32_t width=0U;width<2U;++width)for(uint32_t soft=0U;soft<2U;++soft){
            memcpy(channel,channels[choice],sizeof(channel));smooth(channel,width?21U:7U);
            double weight[600],sorted[600],median_weight=1.0,pilot_step=0.0;uint32_t j=0U;
            for(uint32_t k=0U;k<601U;++k)if(k!=300U){weight[j]=sorted[j]=pow(cabs(channel[k]),2.0);++j;}
            qsort(sorted,600U,sizeof(double),double_order);const double cap=sorted[449]+.5*(sorted[450]-sorted[449]);
            for(uint32_t k=0U;k<600U;++k){weight[k]=WRJ_MIN(weight[k],cap);sorted[k]=weight[k];}median_weight=median(sorted,600U);
            if(pilots==2U){double mag[601];for(uint32_t k=0U;k<601U;++k)mag[k]=cabs(channels[1][k]*conj(channels[0][k]));
                qsort(mag,601U,sizeof(double),double_order);const double limit=mag[450];double complex total=0.0;
                for(uint32_t k=0U;k<601U;++k){const double complex v=channels[1][k]*conj(channels[0][k]);const double a=cabs(v);
                    total+=v/WRJ_MAX(a,2.2204460492503131e-16)*WRJ_MIN(a,limit);}pilot_step=carg(total)/2.0;}
            uint32_t used_symbols=0U;static const uint32_t data_symbols[7]={0U,1U,2U,4U,6U,7U,8U};
            for(uint32_t k=0U;k<7U;++k){const uint32_t sym=data_symbols[k];if(!present[sym])break;
                double complex equalized[600],fourth=0.0;uint32_t index=0U;
                for(uint32_t carrier=0U;carrier<601U;++carrier)if(carrier!=300U){const double complex z=blocks[sym][212U+carrier]/channel[carrier];equalized[index]=z;
                    const double complex unit=z/WRJ_MAX(cabs(z),2.2204460492503131e-16);fourth+=unit*unit*unit*unit*(soft?weight[index]:1.0);++index;}
                double phase=carg(-fourth)/4.0;if(soft){const double prediction=((double)sym-(choice?5.0:3.0))*pilot_step;phase+=round((prediction-phase)/(WRJ_PI/2.0))*(WRJ_PI/2.0);}
                double quality=1.0;if(soft && legacy){double guard[423],received=0.0;uint32_t guards=0U;
                    for(uint32_t carrier=0U;carrier<1024U;++carrier)if(carrier<212U || carrier>812U)guard[guards++]=pow(cabs(blocks[sym][carrier]),2.0);
                    for(uint32_t carrier=0U;carrier<601U;++carrier)if(carrier!=300U)received+=pow(cabs(blocks[sym][212U+carrier]),2.0)/600.0;
                    double noise=median(guard,guards)/log(2.0);quality=WRJ_MAX(0.0,received-noise)/(received+2.2204460492503131e-16);}
                for(uint32_t carrier=0U;carrier<600U;++carrier)symbols[used_symbols++]=equalized[carrier]*cexp(-I*phase)*(soft?weight[carrier]/(median_weight+2.2204460492503131e-16)*quality:1.0);
            }
            wrj_rx_record_t record=*context;record.start0=start;record.nfft=1024U;record.cp=72U;record.active=0U;record.cfo_hz=frequency;
            snprintf(record.phy_mode,sizeof(record.phy_mode),"%s",legacy?"LEGACY_E34_D10D_PI_SHADOW":"MODERN_D10D");
            ++out->stages.symbol_success;
            if(wrj_symbols_to_records(symbols,used_symbols,&record,out)>0)goto finish;
        }
    }
finish:return (int)(out->count-before);
}
static void make_template(double complex *template,uint32_t which,int legacy)
{
    double complex body[1024]={0.0};for(uint32_t k=0U;k<601U;++k)body[(212U+k+512U)%1024U]=pilot(which,k,legacy);
    (void)wrj_dft_complex(body,1024U,1);for(uint32_t k=0U;k<1024U;++k)body[k]*=32.0;
    for(uint32_t k=0U;k<72U;++k)template[k]=body[952U+k];memcpy(template+72U,body,sizeof(body));
}
static uint32_t modern_aliases(const double complex *x,uint32_t n,int *bins)
{
    if(n<4400U)return 0U;wrj_phy_peak_t peaks[64];double phases[64];uint32_t count=wrj_phy_cp_peaks(x,n,1024U,72U,peaks,64U,.55,0);
    for(uint32_t k=0U;k<count;++k)phases[k]=peaks[k].phase;double phase=count?median(phases,count)/1024.0:0.0;
    double complex *mixed=calloc(n,sizeof(*mixed));if(!mixed)return 0U;for(uint32_t k=0U;k<n;++k)mixed[k]=x[k]*cexp(-I*phase*k);
    double complex template[1096];make_template(template,0U,0);double *a=wrj_matched_metric(mixed,n,template,1096U);
    make_template(template,1U,0);double *b=wrj_matched_metric(mixed,n,template,1096U);free(mixed);if(!a||!b){free(a);free(b);return 0U;}
    uint32_t length=n-1096U+1U,used=0U,selected=0U;wrj_phy_peak_t *all=calloc(length,sizeof(*all)),chosen[8];if(!all){free(a);free(b);return 0U;}
    for(uint32_t k=1U;k+1U<length;++k)if(a[k]>=.18 && a[k]>a[k-1U] && a[k]>=a[k+1U])all[used++]=(wrj_phy_peak_t){k,a[k],0.0};qsort(all,used,sizeof(*all),peak_order);
    for(uint32_t k=0U;k<used && selected<8U;++k){int separate=1;for(uint32_t j=0U;j<selected;++j)if(llabs((int64_t)all[k].start-chosen[j].start)<=4384U)separate=0;if(separate)chosen[selected++]=all[k];}
    wrj_phy_peak_t ranking[441];for(int bin=-220;bin<=220;++bin){double delay=fmod(-148.0*1024.0/601.0*bin+512.0,1024.0);if(delay<0.0)delay+=1024.0;delay-=512.0;double score=0.0;
        for(uint32_t k=0U;k<selected;++k){int64_t second=llround(chosen[k].start+2192.0+delay);double best=0.0;for(int d=-3;d<=3;++d){int64_t pos=second+d;if(pos>=0 && pos<length)best=WRJ_MAX(best,b[pos]);}score=WRJ_MAX(score,a[chosen[k].start]*best);}
        ranking[bin+220]=(wrj_phy_peak_t){(uint32_t)(bin+220),score,0.0};}
    qsort(ranking,441U,sizeof(*ranking),peak_order);uint32_t result=selected?16U:0U;for(uint32_t k=0U;k<result;++k)bins[k]=(int)ranking[k].start-220;
    free(all);free(a);free(b);return result;
}
static uint32_t legacy_aliases(const double complex *x,uint32_t n,int *bins)
{
    const uint32_t width=9864U;if(n<4384U)return 0U;double *energies=calloc(n,sizeof(double));if(!energies)return 0U;
    uint32_t a=0U,b=0U;double sum=0.0;for(uint32_t k=0U;k<n;++k){uint32_t left=k>width/2U?k-width/2U:0U,right=WRJ_MIN(n,k+(width-1U)/2U+1U);
        while(b<right){sum+=pow(cabs(x[b]),2.0);++b;}while(a<left){sum-=pow(cabs(x[a]),2.0);++a;}energies[k]=sum/(b-a);}
    double scores[441]={0.0};double complex templates[2][1096];make_template(templates[0],0U,1);make_template(templates[1],1U,1);double ref_energy=0.0;
    for(uint32_t k=0U;k<1096U;++k)ref_energy+=pow(cabs(templates[0][k]),2.0);
    for(uint32_t window=0U;window<3U;++window){uint32_t center=0U;for(uint32_t k=1U;k<n;++k)if(energies[k]>energies[center])center=k;
        if(!isfinite(energies[center])||energies[center]<=0.0)break;uint32_t first=center>width?center-width:0U,last=WRJ_MIN(n,center+width+1U);
        for(uint32_t k=first;k<last;++k)energies[k]=-INFINITY;uint32_t length=last-first;
        wrj_phy_peak_t peaks[16];double phases[16];uint32_t count=wrj_phy_cp_peaks(x+first,length,1024U,72U,peaks,16U,.4,0);
        for(uint32_t k=0U;k<count;++k)phases[k]=peaks[k].phase;double phase=count?median(phases,count)/1024.0:0.0;
        uint32_t nf=1U;while(nf<length+1095U)nf<<=1U;double complex *shared=calloc(nf,sizeof(*shared)),*kernel1=calloc(nf,sizeof(*shared)),*kernel2=calloc(nf,sizeof(*shared)),*trial=calloc(nf,sizeof(*shared));
        uint32_t valid=length-1096U+1U;double *denominator=calloc(valid,sizeof(double)),*metric1=calloc(valid,sizeof(double)),*metric2=calloc(valid,sizeof(double));
        if(!shared||!kernel1||!kernel2||!trial||!denominator||!metric1||!metric2){free(shared);free(kernel1);free(kernel2);free(trial);free(denominator);free(metric1);free(metric2);break;}
        for(uint32_t k=0U;k<length;++k)shared[k]=x[first+k]*cexp(-I*phase*k);sum=0.0;for(uint32_t k=0U;k<1096U;++k)sum+=pow(cabs(shared[k]),2.0);
        for(uint32_t k=0U;k<valid;++k){denominator[k]=sqrt(WRJ_MAX(sum,0.0)*ref_energy+2.2204460492503131e-16);if(k+1096U<length)sum+=pow(cabs(shared[k+1096U]),2.0)-pow(cabs(shared[k]),2.0);}
        for(uint32_t k=0U;k<1096U;++k){kernel1[k]=conj(templates[0][1095U-k]);kernel2[k]=conj(templates[1][1095U-k]);}
        (void)wrj_dft_complex(shared,nf,0);(void)wrj_dft_complex(kernel1,nf,0);(void)wrj_dft_complex(kernel2,nf,0);
        for(int bin=-220;bin<=220;++bin){int64_t shift=(int64_t)bin*(nf/1024U);
            for(uint32_t k=0U;k<nf;++k){int64_t pos=((int64_t)k-shift)%nf;if(pos<0)pos+=nf;trial[k]=shared[k]*kernel1[pos];}(void)wrj_dft_complex(trial,nf,1);
            for(uint32_t k=0U;k<valid;++k)metric1[k]=cabs(trial[k+1095U])/denominator[k];
            for(uint32_t k=0U;k<nf;++k){int64_t pos=((int64_t)k-shift)%nf;if(pos<0)pos+=nf;trial[k]=shared[k]*kernel2[pos];}(void)wrj_dft_complex(trial,nf,1);
            for(uint32_t k=0U;k<valid;++k)metric2[k]=cabs(trial[k+1095U])/denominator[k];
            for(uint32_t k=0U;k+2192U<valid;++k){double partner=0.0;uint32_t p=k+2192U,left=p>4U?p-4U:0U,right=WRJ_MIN(valid,p+5U);
                for(uint32_t j=left;j<right;++j)partner=WRJ_MAX(partner,metric2[j]);scores[bin+220]=WRJ_MAX(scores[bin+220],metric1[k]*partner);}
        }
        free(shared);free(kernel1);free(kernel2);free(trial);free(denominator);free(metric1);free(metric2);
    }
    free(energies);wrj_phy_peak_t ranking[441];for(uint32_t k=0U;k<441U;++k)ranking[k]=(wrj_phy_peak_t){k,scores[k],0.0};qsort(ranking,441U,sizeof(*ranking),peak_order);
    uint32_t count=0U;for(uint32_t k=0U;k<8U && ranking[k].score>.04;++k)bins[count++]=(int)ranking[k].start-220;return count;
}
int wrj_drone_receive(const double complex *x,uint32_t n,double fs,const wrj_rx_record_t *context,wrj_parse_result_t *out,int deep)
{
    uint32_t count=0U;double complex *y=wrj_resample64(x,n,15360000U,(uint32_t)lround(fs),&count);if(!y)return 0;
    const uint32_t before=out->count;int found=decode_family(y,count,15.36e6,context,out,0);
    if(!found && deep)found=decode_family(y,count,15.36e6,context,out,1);
    if(!found && deep && count>=10000U){int bins[48],legacy_bins[8];uint32_t used=modern_aliases(y,count,bins),legacy_count=legacy_aliases(y,count,legacy_bins);
        for(uint32_t k=0U;k<legacy_count;++k){int duplicate=0;for(uint32_t j=0U;j<used;++j)duplicate|=bins[j]==legacy_bins[k];if(!duplicate)bins[used++]=legacy_bins[k];}
        double power[1024]={0.0};double window[1024];double norm=0.0;for(uint32_t k=0U;k<1024U;++k){window[k]=.5-.5*cos(2.0*WRJ_PI*k/1023.0);norm+=window[k]*window[k];}
        for(uint32_t first=0U;first+1024U<=count;first+=512U){double complex block[1024];double complex mean=0.0;for(uint32_t k=0U;k<1024U;++k)mean+=y[first+k]/1024.0;
            for(uint32_t k=0U;k<1024U;++k)block[k]=(y[first+k]-mean)*window[k];(void)wrj_dft_complex(block,1024U,0);
            for(uint32_t k=0U;k<1024U;++k)power[k]+=pow(cabs(block[(k+512U)%1024U]),2.0)/norm;}
        double sorted[1024];memcpy(sorted,power,sizeof(sorted));qsort(sorted,1024U,sizeof(double),double_order);
        double threshold=sorted[153]+.1*(sorted[154]-sorted[153])+.18*(sorted[972]+.3*(sorted[973]-sorted[972])-sorted[153]-.1*(sorted[154]-sorted[153]));
        uint32_t left=1024U,right=0U;for(uint32_t k=0U;k<1024U;++k)if(power[k]>threshold){left=WRJ_MIN(left,k);right=k;}
        int estimate=left==1024U?0:(int)lround((left+right)/2.0-512.0);
        for(int bin=estimate-6;bin<=estimate+6;++bin){int duplicate=0;for(uint32_t j=0U;j<used;++j)duplicate|=bins[j]==bin;if(!duplicate && used<48U)bins[used++]=bin;}
        double complex *mixed=calloc(count,sizeof(*mixed));if(mixed){for(uint32_t k=0U;k<used;++k){int bin=bins[k];if(!bin||abs(bin)>220)continue;
                for(uint32_t j=0U;j<count;++j)mixed[j]=y[j]*cexp(-I*2.0*WRJ_PI*bin*j/1024.0);
                found=decode_family(mixed,count,15.36e6,context,out,0);int legacy=bin>=estimate-6&&bin<=estimate+6;
                for(uint32_t j=0U;j<legacy_count;++j)legacy|=bin==legacy_bins[j];if(!found&&legacy)found=decode_family(mixed,count,15.36e6,context,out,1);
                if(found){for(uint32_t j=before;j<out->count;++j)out->records[j].cfo_hz+=bin*15.36e6/1024.0;break;}}
            free(mixed);}
    }
    for(uint32_t k=before;k<out->count;++k)out->records[k].start0=(uint32_t)lround(out->records[k].start0*fs/15.36e6);
    free(y);return found;
}
