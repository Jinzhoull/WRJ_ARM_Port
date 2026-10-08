#include "wrj_dsp64.h"
#include <stdlib.h>

wrj_status_t wrj_fft64(double *re, double *im, uint32_t n, int inverse)
{
    uint32_t i, j = 0U;
    if (!re || !im || !n || (n & (n-1U))) return WRJ_ERR_ARGUMENT;
    for (i=1U; i<n; ++i) {
        uint32_t bit=n>>1U;
        while (j & bit) { j^=bit; bit>>=1U; }
        j^=bit;
        if (i<j) { double t=re[i]; re[i]=re[j]; re[j]=t;
            t=im[i]; im[i]=im[j]; im[j]=t; }
    }
    for (uint32_t span=2U; span<=n; span<<=1U) {
        const uint32_t half=span/2U;
        const double angle=(inverse ? 2.0 : -2.0)*WRJ_PI/(double)span;
        const double ar=cos(angle), ai=sin(angle);
        double wr=1.0, wi=0.0;
        for (j=0U; j<half; ++j) {
            for (i=j; i<n; i+=span) {
                const uint32_t k=i+half;
                const double tr=wr*re[k]-wi*im[k], ti=wr*im[k]+wi*re[k];
                re[k]=re[i]-tr; im[k]=im[i]-ti; re[i]+=tr; im[i]+=ti;
            }
            const double t=wr*ar-wi*ai;
            wi=wr*ai+wi*ar; wr=t;
        }
        if (span==n) break;
    }
    if (inverse) for (i=0U; i<n; ++i) { re[i]/=n; im[i]/=n; }
    return WRJ_OK;
}

wrj_status_t wrj_bandlimit64(wrj_cf32_t *iq, uint32_t n, double fs, double bw)
{
    uint32_t size=1U, i;
    const double pass=WRJ_MIN(.44*fs,WRJ_MAX(600000.0,.62*bw));
    const double stop=WRJ_MIN(.48*fs,WRJ_MAX(pass+200000.0,.78*bw));
    double *r=NULL,*v=NULL,*kr=NULL,*ki=NULL,*xr=NULL,*xi=NULL;
    if (!n || n>UINT32_MAX/2U) return WRJ_ERR_CAPACITY;
    while (size<2U*n-1U) size<<=1U;
    r=calloc(size,sizeof(double)); v=calloc(size,sizeof(double));
    kr=calloc(size,sizeof(double)); ki=calloc(size,sizeof(double));
    xr=calloc(n,sizeof(double)); xi=calloc(n,sizeof(double));
    if (!r || !v || !kr || !ki || !xr || !xi) goto fail;
    for (i=0U; i<n; ++i) {
        const double a=WRJ_PI*(double)(((uint64_t)i*i)%(2ULL*n))/n;
        kr[i]=cos(a); ki[i]=sin(a); xr[i]=iq[i].re; xi[i]=iq[i].im;
        if (i) { kr[size-i]=kr[i]; ki[size-i]=ki[i]; }
    }
    (void)wrj_fft64(kr,ki,size,0);
    for (uint32_t p=0U; p<2U; ++p) {
        memset(r,0,size*sizeof(double)); memset(v,0,size*sizeof(double));
        for (i=0U; i<n; ++i) {
            const double a=WRJ_PI*(double)(((uint64_t)i*i)%(2ULL*n))/n;
            const double c=cos(a),s=sin(a),b=p ? -xi[i] : xi[i];
            r[i]=xr[i]*c+b*s; v[i]=b*c-xr[i]*s;
        }
        (void)wrj_fft64(r,v,size,0);
        for (i=0U; i<size; ++i) { const double a=r[i], b=v[i];
            r[i]=a*kr[i]-b*ki[i]; v[i]=a*ki[i]+b*kr[i]; }
        (void)wrj_fft64(r,v,size,1);
        for (i=0U; i<n; ++i) {
            const double a=WRJ_PI*(double)(((uint64_t)i*i)%(2ULL*n))/n;
            const double c=cos(a),s=sin(a),tr=r[i]*c+v[i]*s,ti=v[i]*c-r[i]*s;
            if (!p) {
                const double f=(double)WRJ_MIN(i,n-i)*fs/n;
                const double g=f>=stop ? 0.0 : f<=pass ? 1.0 :
                    .5+.5*cos(WRJ_PI*(f-pass)/(stop-pass));
                xr[i]=tr*g; xi[i]=ti*g;
            } else { iq[i].re=(float)(tr/n); iq[i].im=(float)(-ti/n); }
        }
    }
    free(r);free(v);free(kr);free(ki);free(xr);free(xi); return WRJ_OK;
fail:
    free(r);free(v);free(kr);free(ki);free(xr);free(xi); return WRJ_ERR_MEMORY;
}
