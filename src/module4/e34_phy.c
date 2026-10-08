#include "wrj_phy.h"
#include "wrj_dsp64.h"
#include <stdlib.h>
static int wrj_double_numeric_compare(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);}
double complex *wrj_iq64_normalized(const wrj_cf32_t *iq,uint32_t n)
{
    double complex *x=calloc(n,sizeof(*x));double energy=0.0;
    if(!x)return NULL;
    for(uint32_t k=0U;k<n;++k){x[k]=(double)iq[k].re+I*(double)iq[k].im;energy+=creal(x[k]*conj(x[k]));}
    const double rms=sqrt(energy/n)+2.2204460492503131e-16;
    for(uint32_t k=0U;k<n;++k)x[k]/=rms;return x;
}
int wrj_solve_complex(double complex *a,double complex *b,uint32_t n)
{
    for(uint32_t k=0U;k<n;++k){uint32_t pivot=k;
        for(uint32_t j=k+1U;j<n;++j)if(cabs(a[j*n+k])>cabs(a[pivot*n+k]))pivot=j;
        if(cabs(a[pivot*n+k])<1e-20)return 0;
        if(pivot!=k){for(uint32_t j=0U;j<n;++j){const double complex t=a[k*n+j];a[k*n+j]=a[pivot*n+j];a[pivot*n+j]=t;}
            const double complex t=b[k];b[k]=b[pivot];b[pivot]=t;}
        const double complex divisor=a[k*n+k];for(uint32_t j=k;j<n;++j)a[k*n+j]/=divisor;b[k]/=divisor;
        for(uint32_t row=0U;row<n;++row)if(row!=k){const double complex multiplier=a[row*n+k];
            for(uint32_t j=k;j<n;++j)a[row*n+j]-=multiplier*a[k*n+j];b[row]-=multiplier*b[k];}}
    return 1;
}
wrj_status_t wrj_dft_complex(double complex *x,uint32_t n,int inverse)
{
    uint32_t size=1U;const int radix=(n&(n-1U))==0U;
    while(size<(radix?n:2U*n-1U))size<<=1U;
    double *r=calloc(size,sizeof(double)),*v=calloc(size,sizeof(double));
    double *kr=NULL,*ki=NULL;if(!r || !v)goto fail;
    if(radix){for(uint32_t k=0U;k<n;++k){r[k]=creal(x[k]);v[k]=cimag(x[k]);}
        (void)wrj_fft64(r,v,n,inverse);
        for(uint32_t k=0U;k<n;++k)x[k]=r[k]+I*v[k];}
    else {
        kr=calloc(size,sizeof(double));ki=calloc(size,sizeof(double));if(!kr || !ki)goto fail;
        /* 768 is the only non-radix OFDM grid. Its Bluestein reference is
         * invariant; received data is never cached in this plan. */
        static double kernel_re[2048],kernel_im[2048];static int kernel_ready=0;
        const int reuse_kernel=n==768U && size==2048U && kernel_ready;
        for(uint32_t k=0U;k<n;++k){
            const double a=WRJ_PI*(double)(((uint64_t)k*k)%(2ULL*n))/n;
            const double complex z=(inverse?conj(x[k]):x[k])*cexp(-I*a);
            r[k]=creal(z);v[k]=cimag(z);kr[k]=cos(a);ki[k]=sin(a);
            if(k){kr[size-k]=kr[k];ki[size-k]=ki[k];}}
        (void)wrj_fft64(r,v,size,0);
        if(reuse_kernel){memcpy(kr,kernel_re,sizeof(kernel_re));memcpy(ki,kernel_im,sizeof(kernel_im));}
        else{(void)wrj_fft64(kr,ki,size,0);
            if(n==768U && size==2048U){memcpy(kernel_re,kr,sizeof(kernel_re));memcpy(kernel_im,ki,sizeof(kernel_im));kernel_ready=1;}}
        for(uint32_t k=0U;k<size;++k){const double a=r[k],b=v[k];r[k]=a*kr[k]-b*ki[k];v[k]=a*ki[k]+b*kr[k];}
        (void)wrj_fft64(r,v,size,1);
        for(uint32_t k=0U;k<n;++k){const double a=WRJ_PI*(double)(((uint64_t)k*k)%(2ULL*n))/n;
            const double complex z=(r[k]+I*v[k])*cexp(-I*a);x[k]=inverse?conj(z)/n:z;}
    }
    free(r);free(v);free(kr);free(ki);return WRJ_OK;
fail:free(r);free(v);free(kr);free(ki);return WRJ_ERR_MEMORY;
}
static int peak_desc(const void *a,const void *b)
{const wrj_phy_peak_t *x=a,*y=b;if(x->score!=y->score)return(x->score<y->score)-(x->score>y->score);
 return(x->start>y->start)-(x->start<y->start);}
double *wrj_matched_metric(const double complex *x,uint32_t n,const double complex *ref,uint32_t length)
{
    if(n<length || !length)return NULL;uint32_t size=1U;while(size<n+length-1U)size<<=1U;
    double *r=calloc(size,sizeof(double)),*v=calloc(size,sizeof(double)),*tr=calloc(size,sizeof(double)),*ti=calloc(size,sizeof(double));
    double *metric=calloc(n-length+1U,sizeof(double));double ref_energy=0.0,window=0.0;
    if(!r || !v || !tr || !ti || !metric){free(metric);metric=NULL;goto finish;}
    for(uint32_t k=0U;k<n;++k){r[k]=creal(x[k]);v[k]=cimag(x[k]);}
    for(uint32_t k=0U;k<length;++k){const double complex z=conj(ref[length-1U-k]);tr[k]=creal(z);ti[k]=cimag(z);
        ref_energy+=pow(cabs(ref[k]),2.0);window+=pow(cabs(x[k]),2.0);}
    (void)wrj_fft64(r,v,size,0);(void)wrj_fft64(tr,ti,size,0);
    for(uint32_t k=0U;k<size;++k){const double a=r[k],b=v[k];r[k]=a*tr[k]-b*ti[k];v[k]=a*ti[k]+b*tr[k];}
    (void)wrj_fft64(r,v,size,1);
    for(uint32_t k=0U;k+length<=n;++k){metric[k]=hypot(r[k+length-1U],v[k+length-1U])/sqrt(WRJ_MAX(window,0.0)*ref_energy+2.2204460492503131e-16);
        if(k+length<n)window+=pow(cabs(x[k+length]),2.0)-pow(cabs(x[k]),2.0);}
finish:free(r);free(v);free(tr);free(ti);return metric;
}
static double bessel_i0(double x)
{double sum=1.0,term=1.0;for(uint32_t k=1U;k<64U;++k){term*=x*x/(4.0*k*k);sum+=term;if(term<sum*1e-16)break;}return sum;}
double complex *wrj_resample64(const double complex *x,uint32_t n,uint32_t p,uint32_t q,uint32_t *out_count)
{
    if(!x || !p || !q || !out_count)return NULL;
    uint32_t a=p,b=q;while(b){const uint32_t t=a%b;a=b;b=t;}p/=a;q/=a;
    const uint32_t m=WRJ_MAX(p,q);if(m>2048U)return NULL;const uint32_t half=10U*m;
    const uint32_t length=(uint32_t)(((uint64_t)n*p+q-1U)/q);
    double complex *out=calloc(length,sizeof(*out));if(!out)return NULL;
    *out_count=length;if(p==q){memcpy(out,x,length*sizeof(*x));return out;}
    double *h=calloc(2U*half+1U,sizeof(double));if(!h){free(out);return NULL;}double total=0.0;
    for(uint32_t k=0U;k<=2U*half;++k){const double d=(double)k-half;
        h[k]=(fabs(d)<1e-12?1.0/m:sin(WRJ_PI*d/m)/(WRJ_PI*d))*
            bessel_i0(5.0*sqrt(WRJ_MAX(0.0,1.0-d*d/(half*half))))/bessel_i0(5.0);total+=h[k];}
    for(uint32_t k=0U;k<=2U*half;++k)h[k]*=p/total;
    for(uint32_t k=0U;k<length;++k){const int64_t pos=(int64_t)k*q;
        const int64_t first=WRJ_MAX(0,(pos-(int64_t)half+(int64_t)p-1)/(int64_t)p),last=WRJ_MIN(n-1U,(pos+half)/p);
        for(int64_t j=first;j<=last;++j)out[k]+=h[pos-j*p+half]*x[j];}
    free(h);return out;
}
static int peak_time(const void *a,const void *b)
{const wrj_phy_peak_t *x=a,*y=b;return(x->start>y->start)-(x->start<y->start);}
/* Burst edges depend on IQ/fs only, not on FFT/CP numerology. */
static uint32_t burst_regions[64][2],burst_region_count;
static int burst_cache_enabled,burst_cache_ready;
void wrj_phy_begin_ofdm_cache(void){burst_cache_enabled=1;burst_cache_ready=0;burst_region_count=0U;}
void wrj_phy_end_ofdm_cache(void){burst_cache_enabled=0;burst_cache_ready=0;}
static uint32_t cp_peaks_impl(const double complex *x,uint32_t n,uint32_t nf,uint32_t cp,
    wrj_phy_peak_t *out,uint32_t limit,double minimum,int distributed,double fs)
{
    if(n<nf+cp)return 0U;const uint32_t count=n-nf-cp+1U,distance=WRJ_MAX(cp,nf/2U);
    double *metric=calloc(count,sizeof(double)),*power=calloc(count,sizeof(double)),*phase=calloc(count,sizeof(double));
    wrj_phy_peak_t *peaks=calloc(count,sizeof(*peaks));uint8_t *blocked=calloc(count,1U);
    uint32_t used=0U,selected=0U;double complex cross=0.0;double pa=0.0,pb=0.0,max_power=0.0;
    if(!metric || !power || !phase || !peaks || !blocked)goto finish;
    for(uint32_t k=0U;k<cp;++k){cross+=conj(x[k])*x[k+nf];pa+=pow(cabs(x[k]),2.0);pb+=pow(cabs(x[k+nf]),2.0);}
    for(uint32_t k=0U;k<count;++k){
        power[k]=pa;max_power=WRJ_MAX(pa,max_power);metric[k]=cabs(cross)/sqrt(WRJ_MAX(pa*pb,0.0)+2.2204460492503131e-16);phase[k]=carg(cross);
        if(k+1U<count){cross+=conj(x[k+cp])*x[k+cp+nf]-conj(x[k])*x[k+nf];
            pa+=pow(cabs(x[k+cp]),2.0)-pow(cabs(x[k]),2.0);
            pb+=pow(cabs(x[k+cp+nf]),2.0)-pow(cabs(x[k+nf]),2.0);}}
    for(uint32_t k=0U;k<count;++k)if(power[k]<(distributed?.015:.03)*max_power)metric[k]=0.0;
    if(count<=distance+1U){uint32_t best=0U;for(uint32_t k=1U;k<count;++k)if(metric[k]>metric[best])best=k;
        if(metric[best]>minimum){out[0]=(wrj_phy_peak_t){best,metric[best],phase[best]};selected=1U;}goto finish;}
    for(uint32_t k=1U;k+1U<count;++k)if(metric[k]>minimum && metric[k]>metric[k-1U] && metric[k]>=metric[k+1U]){
        peaks[used++]=(wrj_phy_peak_t){k,metric[k],phase[k]};}
    qsort(peaks,used,sizeof(*peaks),peak_desc);
    for(uint32_t k=0U;k<used;++k){const uint32_t pos=peaks[k].start;if(blocked[pos])continue;
        blocked[pos]=2U;const uint32_t a=pos>distance?pos-distance:0U,b=WRJ_MIN(count-1U,pos+distance);
        for(uint32_t j=a;j<=b;++j)if(blocked[j]!=2U)blocked[j]=1U;}
    if(!distributed){for(uint32_t k=0U;k<used && selected<limit;++k)if(blocked[peaks[k].start]==2U)out[selected++]=peaks[k];}
    else {
        const uint32_t stride=nf+cp,blocks=(count+4U*stride-1U)/(4U*stride);
        uint8_t *quota=calloc(blocks,1U);if(!quota)goto finish;
        for(uint32_t k=0U;k<used && selected<limit;++k){const uint32_t block=peaks[k].start/(4U*stride);
            if(blocked[peaks[k].start]==2U && quota[block]<2U){out[selected++]=peaks[k];++quota[block];}}
        free(quota);
        if(fs>0.0){
          if(!burst_cache_enabled || !burst_cache_ready){
            burst_region_count=0U;
            double *envelope=calloc(n,sizeof(double)),*sorted=calloc(n,sizeof(double));uint8_t *mask=calloc(n,1U);
            if(envelope && sorted && mask){uint32_t width=WRJ_MAX(32U,(uint32_t)lround(fs*1e-6)),a=0U,b=0U;double sum=0.0;
                for(uint32_t k=0U;k<n;++k){uint32_t left=k>width/2U?k-width/2U:0U,right=WRJ_MIN(n,k+(width-1U)/2U+1U);
                    while(b<right){sum+=pow(cabs(x[b]),2.0);++b;}while(a<left){sum-=pow(cabs(x[a]),2.0);++a;}envelope[k]=sorted[k]=sum/(b-a);}
                qsort(sorted,n,sizeof(double),wrj_double_numeric_compare);
                double p15=.15*n-.5,p98=.98*n-.5;uint32_t i15=(uint32_t)floor(p15),i98=(uint32_t)floor(p98);
                double noise=sorted[i15]+(p15-i15)*(sorted[i15+1U]-sorted[i15]),high=sorted[i98]+(p98-i98)*(sorted[i98+1U]-sorted[i98]);
                for(uint32_t k=0U;k<n;++k)mask[k]=(uint8_t)(envelope[k]>noise+.18*(high-noise));
                for(uint32_t k=0U;k<n;){if(mask[k]){++k;continue;}uint32_t first=k;while(k<n&&!mask[k])++k;if(first>0U&&k<n&&k-first-1U<128U)memset(mask+first,1,k-first);}
                for(uint32_t k=0U;k<n && burst_region_count<64U;){if(!mask[k]){++k;continue;}uint32_t first=k;while(k<n&&mask[k])++k;if(k-first<151U)continue;
                    burst_regions[burst_region_count][0]=first;burst_regions[burst_region_count++][1]=k-1U;}

            }
            free(envelope);free(sorted);free(mask);burst_cache_ready=1;
          }
          for(uint32_t region=0U;region<burst_region_count;++region){uint32_t first=burst_regions[region][0],added=0U;
            for(uint32_t j=0U;j<used && added<8U;++j){uint32_t pos=peaks[j].start;
              if(blocked[pos]!=2U || (int64_t)pos<(int64_t)first-2U*stride || pos>first+2U*stride)continue;++added;int duplicate=0;
              for(uint32_t old=0U;old<selected;++old)duplicate|=out[old].start==pos;if(!duplicate && selected<limit)out[selected++]=peaks[j];}
          }
        }
        qsort(out,selected,sizeof(*out),peak_time);
    }
finish:free(metric);free(power);free(phase);free(peaks);free(blocked);return selected;
}
uint32_t wrj_phy_cp_peaks(const double complex *x,uint32_t n,uint32_t nf,uint32_t cp,
    wrj_phy_peak_t *out,uint32_t limit,double minimum,int distributed)
{return cp_peaks_impl(x,n,nf,cp,out,limit,minimum,distributed,0.0);}
uint32_t wrj_phy_coarse_ofdm_peaks(const double complex *x,uint32_t n,double fs,uint32_t nf,uint32_t cp,
    wrj_phy_peak_t *out,uint32_t limit)
{return cp_peaks_impl(x,n,nf,cp,out,limit,.25,1,fs);}
