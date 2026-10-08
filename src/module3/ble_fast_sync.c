#include "wrj_dsp64.h"
#include <stdlib.h>

typedef struct { uint32_t start; double value; } ble_peak64_t;
static int compare_double(const void *a,const void *b)
{ const double x=*(const double *)a,y=*(const double *)b; return (x>y)-(x<y); }
static int compare_peak(const void *a,const void *b)
{
    const ble_peak64_t *x=a,*y=b;
    if (x->value!=y->value) return (x->value<y->value)-(x->value>y->value);
    return (x->start>y->start)-(x->start<y->start);
}
static double sorted_quantile(const double *v,uint32_t n,double p)
{
    const double f=p*(n-1U); const uint32_t i=(uint32_t)floor(f);
    return v[i]+(f-i)*(v[WRJ_MIN(i+1U,n-1U)]-v[i]);
}
static uint32_t lower_bound(const double *v,uint32_t n,double value)
{
    uint32_t a=0U,b=n;
    while (a<b) { const uint32_t m=a+(b-a)/2U;
        if (v[m]<value) a=m+1U; else b=m; }
    return a;
}
static void moving_mean(const double *v,double *out,uint32_t n,uint32_t w)
{
    uint32_t left=0U,right=0U; double sum=0.0;
    for (uint32_t i=0U;i<n;++i) {
        const uint32_t a=i>w/2U ? i-w/2U : 0U;
        const uint32_t b=WRJ_MIN(n,i+(w-1U)/2U+1U);
        while (right<b) sum+=v[right++];
        while (left<a) sum-=v[left++];
        out[i]=sum/(right-left);
    }
}
static void remove_moving_median(const double *v,double *out,uint32_t n,uint32_t w,double *window)
{
    uint32_t left=0U,right=0U,used=0U;
    for (uint32_t i=0U;i<n;++i) {
        const uint32_t a=i>w/2U ? i-w/2U : 0U;
        const uint32_t b=WRJ_MIN(n,i+(w-1U)/2U+1U);
        while (left<a) {
            const uint32_t p=lower_bound(window,used,v[left++]);
            memmove(window+p,window+p+1U,(used-p-1U)*sizeof(double)); --used;
        }
        while (right<b) {
            const double x=v[right++]; const uint32_t p=lower_bound(window,used,x);
            memmove(window+p+1U,window+p,(used-p)*sizeof(double));window[p]=x;++used;
        }
        out[i]=v[i]-(used&1U ? window[used/2U] :
            .5*(window[used/2U-1U]+window[used/2U]));
    }
}

/* v22 FAST acquisition has its own 12-peak evidence collection. PDU cache
 * searching must never replace this grid. All CRC observations remain those
 * obtained from IQ before the grid's confidence is independently computed. */
wrj_status_t wrj_ble_fast_sync(const wrj_cf32_t *iq,uint32_t n,double fs,m3_result_t *out)
{
    static const uint8_t aa[4]={0xD6U,0xBEU,0x89U,0x8EU};
    const uint32_t sps=WRJ_MAX(4U,(uint32_t)lround(fs/1e6));
    const uint32_t length=40U*sps,span=WRJ_MAX(5U,2U*(uint32_t)floor(2.5*sps)+1U);
    const uint32_t w=WRJ_MAX(3U,8U*sps),min_sep=WRJ_MAX(1U,(uint32_t)lround(.45e-3*fs));
    uint32_t size=1U,used=0U,selected=0U;
    double *a=NULL,*b=NULL,*r=NULL,*im=NULL,*tr=NULL,*ti=NULL,*ref=NULL,*g=NULL,*window=NULL;
    ble_peak64_t *peaks=NULL,chosen[12];
    if (n<length+2U) return WRJ_ERR_DATA;
    const uint32_t valid=n-length;
    while (size<n-1U+length-1U) size<<=1U;
    a=calloc(n,sizeof(double)); b=calloc(n,sizeof(double));
    r=calloc(size,sizeof(double)); im=calloc(size,sizeof(double));
    tr=calloc(size,sizeof(double)); ti=calloc(size,sizeof(double));
    ref=calloc(length,sizeof(double)); g=calloc(span,sizeof(double));
    window=calloc(w+1U,sizeof(double)); peaks=calloc(valid,sizeof(*peaks));
    if (!a || !b || !r || !im || !tr || !ti || !ref || !g || !window || !peaks) goto fail;
    double sum=0.0;
    for (uint32_t i=0U;i<span;++i) {
        const double x=((double)i-(span-1U)/2.0)/(.38*sps);
        g[i]=exp(-.5*x*x); sum+=g[i];
    }
    for (uint32_t i=0U;i<span;++i) g[i]/=sum;
    for (uint32_t i=0U;i<length;++i) {
        double v=0.0;
        for (uint32_t j=0U;j<span;++j) {
            const int64_t p=(int64_t)i+(span-1U)/2U-j;
            if (p>=0 && p<(int64_t)length) {
                const uint32_t bit=(uint32_t)p/sps;
                const uint32_t sign=bit<8U ? bit&1U : (aa[(bit-8U)/8U]>>((bit-8U)&7U))&1U;
                v+=g[j]*(sign ? 1.0 : -1.0);
            }
        }
        ref[i]=v;
    }
    for (uint32_t i=0U;i<n-1U;++i) {
        const wrj_cf32_t x=iq[i],y=iq[i+1U];
        a[i]=atan2((double)x.re*y.im-(double)x.im*y.re,
            (double)x.re*y.re+(double)x.im*y.im);
    }
    moving_mean(a,b,n-1U,WRJ_MAX(3U,(uint32_t)lround(.18*sps)));
    remove_moving_median(b,a,n-1U,w,window);
    double ref_energy=0.0,energy=0.0;
    for (uint32_t i=0U;i<n-1U;++i) r[i]=a[i];
    for (uint32_t i=0U;i<length;++i) {
        tr[i]=ref[length-1U-i];ref_energy+=ref[i]*ref[i];energy+=a[i]*a[i];
    }
    (void)wrj_fft64(r,im,size,0);(void)wrj_fft64(tr,ti,size,0);
    for (uint32_t i=0U;i<size;++i) {
        const double x=r[i],y=im[i];r[i]=x*tr[i]-y*ti[i];im[i]=x*ti[i]+y*tr[i];
    }
    (void)wrj_fft64(r,im,size,1);
    double peak=-1.0;uint32_t peak_index=0U;
    for (uint32_t i=0U;i<valid;++i) {
        const double value=fabs(r[i+length-1U])/sqrt(WRJ_MAX(0.0,energy)*ref_energy+2.2204460492503131e-16);
        b[i]=isfinite(value) ? value : 0.0;
        if (b[i]>peak) {peak=b[i];peak_index=i;}
        if (i+length<n-1U) energy+=a[i+length]*a[i+length]-a[i]*a[i];
    }
    memcpy(a,b,valid*sizeof(double)); qsort(a,valid,sizeof(double),compare_double);
    const double threshold=WRJ_MAX(.12,sorted_quantile(a,valid,.995));
    const double baseline=sorted_quantile(a,valid,.5);
    for (uint32_t i=0U;i<valid;++i) if (b[i]>=threshold) {
        peaks[used].start=i;peaks[used++].value=b[i];
    }
    qsort(peaks,used,sizeof(*peaks),compare_peak);
    for (uint32_t i=0U;i<used && selected<12U;++i) {
        int separated=1;
        for (uint32_t j=0U;j<selected;++j)
            if (llabs((int64_t)peaks[i].start-chosen[j].start)<min_sep) separated=0;
        if (separated) chosen[selected++]=peaks[i];
    }
    if (!selected) {chosen[0].start=peak_index;chosen[0].value=peak;selected=1U;}
    for (uint32_t i=1U;i<selected;++i) {
        const ble_peak64_t value=chosen[i];uint32_t j=i;
        while (j && chosen[j-1U].start>value.start) {chosen[j]=chosen[j-1U];--j;}chosen[j]=value;
    }
    double repeat=0.0;
    if (selected>=3U) {
        double mean=0.0,var=0.0;
        for (uint32_t i=1U;i<selected;++i) mean+=chosen[i].start-chosen[i-1U].start;
        mean/=selected-1U;
        for (uint32_t i=1U;i<selected;++i) {const double d=chosen[i].start-chosen[i-1U].start-mean;var+=d*d;}
        repeat=WRJ_CLAMP(1.0-sqrt(var/(selected-2U))/WRJ_MAX(mean,1.0),0.0,1.0);
    }
    out->num_frames=selected;peak=0.0;
    for (uint32_t i=0U;i<selected;++i) {
        out->frame_start_samples_0based[i]=chosen[i].start;
        out->frame_confidence[i]=(float)WRJ_CLAMP(.75*WRJ_CLAMP((chosen[i].value-baseline)/
            WRJ_MAX(1.0-baseline,.05),0.0,1.0)+.25*repeat,0.0,1.0);
        peak=WRJ_MAX(peak,chosen[i].value);
        if (i) a[i-1U]=chosen[i].start-chosen[i-1U].start;
    }
    if (selected>=2U) {qsort(a,selected-1U,sizeof(double),compare_double);
        out->frame_length_samples=(uint32_t)lround(sorted_quantile(a,selected-1U,.5));}
    else out->frame_length_samples=WRJ_MAX(length,(uint32_t)lround(.85e-3*fs));
    out->peak_metric=(float)peak;
    out->sync_confidence=(float)WRJ_CLAMP(.72*WRJ_CLAMP((peak-.08)/.42,0.0,1.0)+.28*repeat,0.0,1.0);
    /* M3's status boost requires CRC-verified BasicID and Location. */
    int basic=0,location=0;double corr_sum=0.0;
    for (uint32_t i=0U;i<out->ble_verified_packet_count;++i) {
        const uint8_t *p=out->ble_verified_pdu[i];const uint8_t type=p[14]>>4U;
        basic|=type==0U;location|=type==1U;corr_sum+=out->ble_verified_confidence[i];
    }
    if (basic && location) {
        const double strength=.55*corr_sum/WRJ_MAX(1U,out->ble_verified_packet_count)+
            .25*WRJ_MIN(out->ble_verified_packet_count/2.0,1.0)+.20;
        out->sync_confidence=WRJ_MAX(out->sync_confidence,(float)(.82+.10*
            WRJ_MIN(out->ble_verified_packet_count/4.0,1.0)+.06*strength));
    }
    out->ble_sync_confidence=out->sync_confidence;
    snprintf(out->sync_method,sizeof(out->sync_method),"BLE_1M_preamble_access_address_GFSK_correlation");
    free(a);free(b);free(r);free(im);free(tr);free(ti);free(ref);free(g);free(window);free(peaks);
    return WRJ_OK;
fail:
    free(a);free(b);free(r);free(im);free(tr);free(ti);free(ref);free(g);free(window);free(peaks);
    return WRJ_ERR_MEMORY;
}
