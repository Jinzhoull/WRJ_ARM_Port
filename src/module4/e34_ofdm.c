#include "wrj_phy.h"
#include <stdlib.h>
#include <time.h>
typedef struct {uint32_t first,width,root;double coherence,rank;} grid_t;
typedef struct {
    uint32_t pos,nfft,cp;double hz,fs;uint8_t valid;
    double complex spectrum[2048];
} fft_cache_entry_t;
/* Owned and released by a single receive call; no cross-candidate cache. */
static fft_cache_entry_t *active_fft_cache;
static uint32_t fft_cache_next;
static wrj_parse_result_t *active_metrics;
static double energy(double complex z){return creal(z*conj(z));}
static int numeric_order(const void *a,const void *b)
{const double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);}
static int grid_order(const void *a,const void *b)
{const grid_t *x=a,*y=b;return(x->rank<y->rank)-(x->rank>y->rank);}
static double percentile(double *scratch,uint32_t n,double p,int matlab_prctile)
{
    qsort(scratch,n,sizeof(double),numeric_order);
    const double pos=WRJ_CLAMP(matlab_prctile ? p*n-.5 : p*(n-1U),0.0,n-1.0);
    const uint32_t j=(uint32_t)floor(pos);return scratch[j]+(pos-j)*(scratch[WRJ_MIN(j+1U,n-1U)]-scratch[j]);
}
static double complex zc(uint32_t root,uint32_t n,uint32_t k)
{
    /* Bounded public-reference cache. No candidate bytes or truth retained. */
    static uint32_t roots[64],lengths[64],next=0U,last=0U;
    static uint16_t hints[256];
    static double complex values[64][2048];
    if(lengths[last]==n && roots[last]==root)return values[last][k];
    const uint32_t key=(root*37U+n*17U+(n>>4U))&255U;
    if(hints[key]){const uint32_t j=hints[key]-1U;
        if(lengths[j]==n && roots[j]==root){last=j;return values[j][k];}}
    for(uint32_t j=0U;j<64U;++j)if(lengths[j]==n && roots[j]==root){last=j;hints[key]=(uint16_t)(j+1U);return values[j][k];}
    const uint32_t slot=next++%64U;roots[slot]=root;lengths[slot]=n;
    last=slot;hints[key]=(uint16_t)(slot+1U);
    for(uint32_t j=0U;j<n;++j)values[slot][j]=cexp(-I*WRJ_PI*root*j*(j+1.0)/n);
    return values[slot][k];
}
static double slope(const double complex *h,uint32_t n)
{double complex cross=0.0;for(uint32_t k=1U;k<n;++k)cross+=h[k]*conj(h[k-1U]);return carg(cross);}
static void mean_complex(const double complex *x,double complex *y,uint32_t n,uint32_t width)
{
    uint32_t a=0U,b=0U;double complex sum=0.0;
    for(uint32_t k=0U;k<n;++k){const uint32_t left=k>width/2U?k-width/2U:0U,right=WRJ_MIN(n,k+(width-1U)/2U+1U);
        while(b<right)sum+=x[b++];while(a<left)sum-=x[a++];y[k]=sum/(b-a);}
}
static double channel_score(const double complex *channel,uint32_t n,double linear)
{
    double complex flat[2048],smooth[2048];double residual=0.0,power=0.0;
    for(uint32_t k=0U;k<n;++k)flat[k]=channel[k]*cexp(-I*linear*k);
    mean_complex(flat,smooth,n,9U);
    for(uint32_t k=0U;k<n;++k){residual+=energy(flat[k]-smooth[k]);power+=energy(flat[k]);}
    return sqrt(WRJ_MAX(0.0,1.0-residual/(power+2.2204460492503131e-16)));
}
static void smooth_channel(double complex *h,uint32_t n,uint32_t width)
{
    double complex flat[2048],smooth[2048];const double linear=slope(h,n);
    for(uint32_t k=0U;k<n;++k)flat[k]=h[k]*cexp(-I*linear*k);
    mean_complex(flat,smooth,n,width);
    for(uint32_t k=0U;k<n;++k)h[k]=smooth[k]*cexp(I*linear*k);
}
static int header_present(const double complex *s,uint32_t n,uint32_t width)
{
    static const uint16_t words[6]={0xd1c2U,0xd1b4U,0xd10dU,0xa1c2U,0xa1b4U,0x7e7eU};
    static const uint8_t codes[6]={1U,2U,3U,4U,5U,15U};
    for(uint32_t slot=0U;slot<2U;++slot){const uint32_t start=slot*width;if(start+44U>n)continue;
        for(uint32_t q=0U;q<4U;++q){uint8_t b[11]={0};const double complex rot=cexp(-I*q*WRJ_PI/2.0);
            for(uint32_t k=0U;k<44U;++k){const double complex z=s[start+k]*rot;
                const uint32_t bit=2U*k;b[bit/8U]|=(uint8_t)((creal(z)<0.0)<<(7U-bit%8U));
                b[(bit+1U)/8U]|=(uint8_t)((cimag(z)<0.0)<<(7U-(bit+1U)%8U));}
            const uint32_t word=((uint32_t)b[0]<<8U)|b[1],length=((uint32_t)b[4]<<8U)|b[5];
            for(uint32_t j=0U;j<6U;++j)if(word==words[j] && b[2]==32U+codes[j] && length>=12U && length<=4096U && b[6]<=7U)return 1;
        }}
    return 0;
}
static double cpe(const double complex *z,uint32_t n)
{double complex fourth=0.0;for(uint32_t k=0U;k<n;++k){const double complex unit=z[k]/WRJ_MAX(cabs(z[k]),2.2204460492503131e-16);fourth+=unit*unit*unit*unit;}return carg(-fourth)/4.0;}
static int spectrum_at(const double complex *x,uint32_t n,uint32_t pos,uint32_t nf,uint32_t cp,double hz,double fs,double complex *s)
{
    if(pos+nf+cp>n)return 0;
    if(active_fft_cache)for(uint32_t j=0U;j<24U;++j){fft_cache_entry_t *entry=active_fft_cache+j;
        if(entry->valid && entry->pos==pos && entry->nfft==nf && entry->cp==cp && entry->hz==hz && entry->fs==fs){
            memcpy(s,entry->spectrum,nf*sizeof(*s));if(active_metrics)++active_metrics->fft_cache_hits;return 1;}}
    double complex body[2048];if(nf>2048U)return 0;
    for(uint32_t k=0U;k<nf;++k)body[k]=x[pos+cp+k]*cexp(-I*2.0*WRJ_PI*hz*(pos+cp+k)/fs);
    if(wrj_dft_complex(body,nf,0)!=WRJ_OK)return 0;
    if(active_metrics)++active_metrics->fft_calls;
    for(uint32_t k=0U;k<nf;++k)s[k]=body[(k+(nf+1U)/2U)%nf]/sqrt(nf);
    if(active_fft_cache){fft_cache_entry_t *entry=active_fft_cache+(fft_cache_next++%24U);
        entry->pos=pos;entry->nfft=nf;entry->cp=cp;entry->hz=hz;entry->fs=fs;entry->valid=1U;
        memcpy(entry->spectrum,s,nf*sizeof(*s));}
    return 1;
}
static uint32_t compatible_grid(const double complex *s,uint32_t nf,grid_t *grid)
{
    double power[2048],sorted[2048];uint32_t left=nf,right=0U;
    for(uint32_t k=0U;k<nf;++k)sorted[k]=power[k]=energy(s[k]);
    qsort(sorted,nf,sizeof(double),numeric_order);const uint32_t top=WRJ_MIN(160U,nf);
    const double threshold=.15*percentile(sorted+nf-top,top,.5,0);
    for(uint32_t k=0U;k<nf;++k)if(power[k]>threshold){left=WRJ_MIN(left,k);right=k;}
    if(left==nf || right-left+1U<70U)return 0U;
    const uint32_t width=right-left+1U;if(width&1U || width>nf-16U)return 0U;
    double complex sum=0.0,previous=0.0;
    for(uint32_t k=1U;k<width;++k){const double complex d=s[left+k]*conj(s[left+k-1U]);if(k>=2U)sum+=d*conj(previous);previous=d;}
    int64_t root=(int64_t)llround(-carg(sum)*width/(2.0*WRJ_PI));root%=(int64_t)width;if(root<0)root+=width;
    if(root<1 || root>100)return 0U;
    double complex h[2048];double amplitude=0.0;
    for(uint32_t k=0U;k<width;++k){h[k]=s[left+k]/zc((uint32_t)root,width,k);amplitude+=cabs(h[k]);}
    const double linear=slope(h,width);sum=0.0;
    for(uint32_t k=0U;k<width;++k)sum+=h[k]*cexp(-I*linear*k);
    const double coherence=cabs(sum)/WRJ_MAX(amplitude,2.2204460492503131e-16);
    if(coherence<.8)return 0U;grid[0]=(grid_t){left,width,(uint32_t)root,coherence,coherence};return 1U;
}
static uint32_t single_grid(const double complex *s,uint32_t nf,grid_t *out)
{
    double power[2048],sorted[2048],smoothed[2048],prefix[2049]={0.0};uint8_t mask[2048],closed[2048];
    double total=0.0;
    for(uint32_t k=0U;k<nf;++k){power[k]=sorted[k]=energy(s[k]);total+=power[k];prefix[k+1U]=total;}
    const double noise=percentile(sorted,nf,.25,1);const uint32_t top=WRJ_MIN(160U,nf);
    const double threshold=WRJ_MAX(4.0*noise,.015*percentile(sorted+nf-top,top,.5,0));
    for(uint32_t k=0U;k<nf;++k){double sum=0.0;const uint32_t a=k>2U?k-2U:0U,b=WRJ_MIN(nf,k+3U);
        for(uint32_t j=a;j<b;++j)sum+=power[j];smoothed[k]=sum/(b-a);mask[k]=(uint8_t)(smoothed[k]>threshold);}
    for(uint32_t k=0U;k<nf;++k){uint32_t sum=0U,a=k>3U?k-3U:0U,b=WRJ_MIN(nf,k+4U);
        for(uint32_t j=a;j<b;++j)sum+=mask[j];closed[k]=(uint8_t)(sum>=3U);}
    for(uint32_t k=0U;k<nf;){if(closed[k]){++k;continue;}const uint32_t a=k;while(k<nf && !closed[k])++k;
        if(a>0U && k<nf && k-a-1U<WRJ_MAX(32U,nf/16U))memset(closed+a,1,k-a);}
    uint32_t left=0U,right=0U;double best=-1.0;
    for(uint32_t k=0U;k<nf;){if(!closed[k]){++k;continue;}const uint32_t a=k;while(k<nf && closed[k])++k;
        const double e=prefix[k]-prefix[a];if(e>best){best=e;left=a;right=k-1U;}}
    const uint32_t span=right-left+1U;if(best<0.0 || span<60U || span>nf-8U)return 0U;
    double complex triples[2048];double weights[2048];uint32_t used=0U;
    for(uint32_t k=left+1U;k<right;++k)if(power[k-1U]>2.0*noise && power[k]>2.0*noise && power[k+1U]>2.0*noise){
        triples[used]=s[k+1U]*s[k-1U]*conj(s[k])*conj(s[k]);weights[used]=cabs(triples[used]);++used;}
    if(used<40U)return 0U;const double cap=percentile(weights,used,.70,1);double sumweight=0.0;double complex sum=0.0;
    for(uint32_t k=0U;k<used;++k){const double magnitude=cabs(triples[k]),w=WRJ_MIN(magnitude,cap);
        sum+=triples[k]/WRJ_MAX(magnitude,2.2204460492503131e-16)*w;sumweight+=w;}
    if(cabs(sum)/(sumweight+2.2204460492503131e-16)<.4)return 0U;
    double ratio=fmod(-carg(sum)/(2.0*WRJ_PI),1.0);if(ratio<0.0)ratio+=1.0;
    grid_t trials[1024];uint32_t found=0U;
    const uint32_t first=(uint32_t)(2.0*ceil(WRJ_MAX(70.0,.5*span)/2.0)),last=WRJ_MIN(nf-16U,span+WRJ_MAX(64U,span/2U));
    for(uint32_t width=first;width<=last;width+=2U){const uint32_t root=(uint32_t)lround(ratio*width);
        if(root<1U || root>100U || fabs(ratio-(double)root/width)>.0025)continue;
        uint32_t anchor=0U;double largest=-1.0;
        for(uint32_t k=0U;k+width<=nf;++k){const double e=prefix[k+width]-prefix[k];if(e>largest){largest=e;anchor=k;}}
        double score=-1.0;uint32_t position=anchor;
        for(int offset=-2;offset<=2;++offset){const int64_t a=(int64_t)anchor+offset;if(a<0 || a+width>nf)continue;
            double complex h[2048];for(uint32_t k=0U;k<width;++k)h[k]=s[a+k]/zc(root,width,k);
            const double value=channel_score(h,width,slope(h,width));if(value>score){score=value;position=(uint32_t)a;}}
        if(score<.75)continue;const uint32_t edge=WRJ_MAX(4U,(uint32_t)lround(.035*width));
        const double edgepower=WRJ_MIN(prefix[position+edge]-prefix[position],prefix[position+width]-prefix[position+width-edge])/edge;
        const double support=WRJ_MIN(1.0,sqrt(edgepower/(8.0*noise+2.2204460492503131e-16)));
        const double captured=(prefix[position+width]-prefix[position])/WRJ_MAX(total,2.2204460492503131e-16);
        trials[found++]=(grid_t){position,width,root,score,score*captured*support};
    }
    qsort(trials,found,sizeof(*trials),grid_order);found=WRJ_MIN(found,3U);uint32_t count=0U;
    static const int offsets[5]={0,-2,2,-4,4};
    for(uint32_t j=0U;j<found;++j)for(uint32_t k=0U;k<5U;++k){const int64_t pos=(int64_t)trials[j].first+offsets[k];
        if(pos>=0 && pos+trials[j].width<=nf){out[count]=trials[j];out[count++].first=(uint32_t)pos;}}
    return count;
}
static uint32_t engineering_grid(const double complex *s,uint32_t nf,const double complex *partner,const double complex *payload,grid_t *out)
{
    double power[2048],sorted[2048],weights[2048],prefix[2049]={0.0};double complex triple[2048];uint32_t count=0U;
    for(uint32_t k=0U;k<nf;++k){power[k]=sorted[k]=energy(partner[k]);prefix[k+1U]=prefix[k]+power[k]+energy(s[k]);}
    double noise=percentile(sorted,nf,.25,1),threshold=WRJ_MAX(4.0*noise,.025*percentile(sorted+nf-WRJ_MIN(160U,nf),WRJ_MIN(160U,nf),.5,0));
    for(uint32_t k=1U;k+1U<nf;++k)if(power[k-1U]>threshold&&power[k]>threshold&&power[k+1U]>threshold){
        triple[count]=partner[k+1U]*partner[k-1U]*conj(partner[k])*conj(partner[k]);weights[count]=cabs(triple[count]);++count;}
    if(count<40U)return 0U;double cap=percentile(weights,count,.7,1),total_weight=0.0;double complex total=0.0;
    for(uint32_t k=0U;k<count;++k){double w=WRJ_MIN(cabs(triple[k]),cap);total+=triple[k]/WRJ_MAX(cabs(triple[k]),2.2204460492503131e-16)*w;total_weight+=w;}
    if(cabs(total)/(total_weight+2.2204460492503131e-16)<.35)return 0U;double ratio=fmod(-carg(total)/(2.0*WRJ_PI)+1.0,1.0);
    if(ratio<42.0/(nf-16U)||ratio>42.0/80.0)return 0U;int estimate=2*(int)lround(42.0/(2.0*ratio));grid_t trials[60];uint32_t found=0U;
    for(int width_int=estimate-4;width_int<=estimate+4;width_int+=2){if(width_int<80||width_int>(int)nf-16)continue;uint32_t width=(uint32_t)width_int,anchor=0U;double best=-1.0;
        for(uint32_t k=0U;k+width<=nf;++k){double value=prefix[k+width]-prefix[k];if(value>best){best=value;anchor=k;}}
        double complex cross[2048];for(uint32_t k=0U;k<width;++k)cross[k]=partner[anchor+k]*conj(s[anchor+k])/(zc(42U,width,k)*conj(zc(24U,width,k)));
        double period=width/18.0,shift=-slope(cross,width)*width/(2.0*WRJ_PI*18.0);wrj_phy_peak_t positions[37];uint32_t used=0U;
        for(int k=-18;k<=18;++k){int64_t a=llround(anchor-shift+k*period);if(a<0||a+width>nf||llabs(a-anchor)>width/2U)continue;int duplicate=0;
            for(uint32_t j=0U;j<used;++j)duplicate|=positions[j].start==(uint32_t)a;if(duplicate)continue;positions[used++]=(wrj_phy_peak_t){(uint32_t)a,prefix[a+width]-prefix[a],0.0};}
        for(uint32_t k=1U;k<used;++k){wrj_phy_peak_t value=positions[k];uint32_t j=k;while(j&&positions[j-1U].score<value.score){positions[j]=positions[j-1U];--j;}positions[j]=value;}
        for(uint32_t k=0U;k<WRJ_MIN(12U,used);++k){uint32_t first=positions[k].start;double complex channel[2048],z[2048];
            for(uint32_t j=0U;j<width;++j)channel[j]=partner[first+j]/zc(42U,width,j);double coherence=channel_score(channel,width,slope(channel,width));if(coherence<.8)continue;
            smooth_channel(channel,width,7U);for(uint32_t j=0U;j<width;++j)z[j]=payload[first+j]/channel[j];double phase=cpe(z,width);
            const double complex phase_rotation=cexp(-I*phase);
            for(uint32_t j=0U;j<width;++j)z[j]*=phase_rotation;if(!header_present(z,width,width))continue;
            trials[found++]=(grid_t){first,width,24U,coherence,2.0+coherence};}
    }
    qsort(trials,found,sizeof(*trials),grid_order);found=WRJ_MIN(found,6U);memcpy(out,trials,found*sizeof(*out));return found;
}
typedef struct {uint32_t width,r1,r2,anchor;double cost,rank;} joint_seed_t;
static int seed_order(const void *a,const void *b){const joint_seed_t *x=a,*y=b;return(x->rank<y->rank)-(x->rank>y->rank);}
static uint32_t joint_grid(const double complex *s,uint32_t nf,const double complex *partner,const double complex *payload,grid_t *out)
{
    double power[2048],sorted[2048],prefix[2049]={0.0};uint8_t reliable[2048],mask[2048];
    for(uint32_t k=0U;k<nf;++k){power[k]=sorted[k]=energy(s[k])+energy(partner[k]);prefix[k+1U]=prefix[k]+power[k];}
    double total_power=prefix[nf],noise=percentile(sorted,nf,.25,1),threshold=WRJ_MAX(4.0*noise,.025*percentile(sorted+nf-WRJ_MIN(160U,nf),WRJ_MIN(160U,nf),.5,0));uint32_t good=0U;
    for(uint32_t k=0U;k<nf;++k){reliable[k]=(uint8_t)(power[k]>threshold);good+=reliable[k];}if(good<70U)return 0U;
    for(uint32_t k=0U;k<nf;++k){uint32_t sum=0U,a=k>4U?k-4U:0U,b=WRJ_MIN(nf,k+5U);for(uint32_t j=a;j<b;++j)sum+=reliable[j];mask[k]=(uint8_t)(sum>=5U);}
    for(uint32_t k=0U;k<nf;){if(mask[k]){++k;continue;}uint32_t a=k;while(k<nf&&!mask[k])++k;if(a>0U&&k<nf&&k-a-1U<nf/16U)memset(mask+a,1,k-a);}
    wrj_phy_peak_t regions[1024];uint32_t ends[2048],region_count=0U;
    for(uint32_t k=0U;k<nf;){if(!mask[k]){++k;continue;}uint32_t a=k;while(k<nf&&mask[k])++k;regions[region_count]=(wrj_phy_peak_t){a,prefix[k]-prefix[a],0.0};ends[a]=k-1U;++region_count;}
    for(uint32_t k=1U;k<region_count;++k){wrj_phy_peak_t v=regions[k];uint32_t j=k;while(j&&regions[j-1U].score<v.score){regions[j]=regions[j-1U];--j;}regions[j]=v;}
    grid_t all[36];uint32_t all_count=0U;
    for(uint32_t region=0U;region<WRJ_MIN(3U,region_count);++region){uint32_t left=regions[region].start,right=ends[left],span=right-left+1U;if(span>nf-8U)continue;
        double signatures[2][3],strengths[2][3],ratios[2];int valid=1;
        for(uint32_t pre=0U;pre<2U;++pre){const double complex *z=pre?partner:s;
            for(uint32_t q=0U;q<3U;++q){static const uint32_t lags[3]={1U,3U,5U};uint32_t lag=lags[q],used=0U;double weights[2048];double complex triple[2048],sum=0.0;double sum_weight=0.0;
                if(right<=left+2U*lag){valid=0;break;}
                for(uint32_t k=left+lag;k+lag<=right;++k){triple[used]=z[k+lag]*z[k-lag]*conj(z[k])*conj(z[k]);weights[used]=cabs(triple[used]);++used;}
                double cap=percentile(weights,used,.6,1);for(uint32_t k=0U;k<used;++k){double w=WRJ_MIN(cabs(triple[k]),cap);sum+=triple[k]/WRJ_MAX(cabs(triple[k]),2.2204460492503131e-16)*w;sum_weight+=w;}
                signatures[pre][q]=carg(sum);strengths[pre][q]=cabs(sum)/(sum_weight+2.2204460492503131e-16);
            }
            ratios[pre]=fmod(-signatures[pre][0]/(2.0*WRJ_PI)+1.0,1.0);
        }
        if(!valid || WRJ_MIN(strengths[0][0],strengths[1][0])<.25)continue;
        joint_seed_t seeds[1024];uint32_t used=0U;uint32_t maximum=WRJ_MIN(nf-16U,(uint32_t)(span+WRJ_MAX(64.0,.4*span)));
        for(uint32_t width=70U;width<=maximum;width+=2U){uint32_t roots[2]={(uint32_t)lround(ratios[0]*width),(uint32_t)lround(ratios[1]*width)};
            if(roots[0]<1U||roots[0]>100U||roots[1]<1U||roots[1]>100U||roots[0]==roots[1])continue;double cost=0.0;
            for(uint32_t pre=0U;pre<2U;++pre)for(uint32_t q=0U;q<3U;++q){static const uint32_t squares[3]={1U,9U,25U};double error=carg(cexp(I*(signatures[pre][q]+2.0*WRJ_PI*roots[pre]*squares[q]/width)));cost+=strengths[pre][q]*error*error;}
            if(cost>.25)continue;uint32_t anchor=0U;double captured=-1.0;for(uint32_t k=0U;k+width<=nf;++k){double value=prefix[k+width]-prefix[k];if(value>captured){captured=value;anchor=k;}}
            seeds[used++]=(joint_seed_t){width,roots[0],roots[1],anchor,cost,exp(-20.0*cost)*sqrt(captured/(total_power+2.2204460492503131e-16))};
        }
        qsort(seeds,used,sizeof(*seeds),seed_order);grid_t trials[12];uint32_t found=0U;
        for(uint32_t index=0U;index<WRJ_MIN(12U,used);++index){const joint_seed_t *seed=seeds+index;uint32_t width=seed->width,anchor=seed->anchor;
            double complex relative[2048];for(uint32_t k=0U;k<width;++k)relative[k]=partner[anchor+k]*conj(s[anchor+k])/(zc(seed->r2,width,k)*conj(zc(seed->r1,width,k)));
            double difference=(double)seed->r2-seed->r1,shift=-slope(relative,width)*width/(2.0*WRJ_PI*difference),period=width/fabs(difference);int radius=(int)ceil(width/period);
            wrj_phy_peak_t positions[201];uint32_t positions_count=0U;
            for(int k=-radius;k<=radius;++k){int64_t a=llround(anchor-shift-k*period);if(a<0 || a+width>nf || llabs(a-anchor)>WRJ_MAX(32U,width/3U))continue;int duplicate=0;
                for(uint32_t j=0U;j<positions_count;++j)duplicate|=positions[j].start==(uint32_t)a;if(!duplicate)positions[positions_count++]=(wrj_phy_peak_t){(uint32_t)a,prefix[a+width]-prefix[a],0.0};}
            for(uint32_t k=1U;k<positions_count;++k){wrj_phy_peak_t value=positions[k];uint32_t j=k;while(j&&positions[j-1U].score<value.score){positions[j]=positions[j-1U];--j;}positions[j]=value;}
            double best=0.0;uint32_t best_first=anchor;
            for(uint32_t p=0U;p<WRJ_MIN(12U,positions_count);++p){uint32_t first=positions[p].start;double complex h[2][2048],cross=0.0;double weights[2048],sum_weight=0.0;
                for(uint32_t k=0U;k<width;++k){h[0][k]=s[first+k]/zc(seed->r1,width,k);h[1][k]=partner[first+k]/zc(seed->r2,width,k);weights[k]=cabs(h[0][k])*cabs(h[1][k]);}
                double cap=percentile(weights,width,.75,1);for(uint32_t k=0U;k<width;++k){double w=WRJ_MIN(cabs(h[0][k])*cabs(h[1][k]),cap);double complex unit=h[1][k]*conj(h[0][k]);cross+=unit/WRJ_MAX(cabs(unit),2.2204460492503131e-16)*w;sum_weight+=w;}
                double coherence=cabs(cross)/(sum_weight+2.2204460492503131e-16),coverage=(prefix[first+width]-prefix[first])/(total_power+2.2204460492503131e-16);
                double score=coherence*sqrt(coverage)*exp(-5.0*seed->cost);
                for(uint32_t pilot=0U;pilot<2U;++pilot){smooth_channel(h[pilot],width,7U);double complex z[2048];for(uint32_t k=0U;k<width;++k)z[k]=payload[first+k]/h[pilot][k];double phase=cpe(z,width);
                    const double complex phase_rotation=cexp(-I*phase);
                    for(uint32_t k=0U;k<width;++k)z[k]*=phase_rotation;if(header_present(z,width,width)){score+=2.0;break;}}
                if(score>best){best=score;best_first=first;}
            }
            if(best>.55)trials[found++]=(grid_t){best_first,width,seed->r1,best,best};
        }
        qsort(trials,found,sizeof(*trials),grid_order);for(uint32_t k=0U;k<WRJ_MIN(4U,found);++k){static const int offsets[3]={0,-1,1};for(uint32_t j=0U;j<3U;++j){int64_t first=(int64_t)trials[k].first+offsets[j];
                if(first>=0 && first+trials[k].width<=nf){all[all_count]=trials[k];all[all_count++].first=(uint32_t)first;}}}
    }
    qsort(all,all_count,sizeof(*all),grid_order);all_count=WRJ_MIN(all_count,12U);memcpy(out,all,all_count*sizeof(*out));return all_count;
}
static void denoise_channel(double complex *channel,uint32_t nf,uint32_t first,uint32_t width)
{
    double complex body[2048]={0.0},matrix[25*25]={0.0},rhs[25]={0.0},design[25];
    for(uint32_t k=0U;k<width;++k)body[(first+k+nf/2U)%nf]=channel[k];(void)wrj_dft_complex(body,nf,1);
    uint32_t peak=0U;for(uint32_t k=1U;k<nf;++k)if(cabs(body[k])>cabs(body[peak]))peak=k;
    uint32_t taps[25];for(uint32_t k=0U;k<25U;++k)taps[k]=(peak+nf+k-6U)%nf;
    for(uint32_t k=0U;k<width;++k){double frequency=(double)first-nf/2U+k;
        for(uint32_t j=0U;j<25U;++j)design[j]=cexp(-I*2.0*WRJ_PI*frequency*taps[j]/nf);
        for(uint32_t j=0U;j<25U;++j){rhs[j]+=conj(design[j])*channel[k];for(uint32_t l=0U;l<25U;++l)matrix[j*25U+l]+=conj(design[j])*design[l];}}
    for(uint32_t k=0U;k<25U;++k)matrix[k*25U+k]+=.003*width;
    if(!wrj_solve_complex(matrix,rhs,25U))return;
    for(uint32_t k=0U;k<width;++k){double frequency=(double)first-nf/2U+k;channel[k]=0.0;
        for(uint32_t j=0U;j<25U;++j)channel[k]+=cexp(-I*2.0*WRJ_PI*frequency*taps[j]/nf)*rhs[j];}
}
static int tail_restore(const double complex *x,uint32_t n,uint32_t start,uint32_t nf,uint32_t cp,double frequency,double fs,uint32_t first,uint32_t width,
    const double complex *channel,double complex *symbols,uint32_t used,const wrj_rx_record_t *record,wrj_parse_result_t *out)
{
    uint32_t stride=nf+cp;double complex repaired[2048],prefix[2048],spectrum[2048];uint8_t header[13];
    for(uint32_t q=0U;q<4U;++q)for(uint32_t offset=0U;offset<2U;++offset){if((offset*width+52U)>used)continue;memset(header,0,sizeof(header));
        double complex rotation=cexp(-I*q*WRJ_PI/2.0);
        for(uint32_t k=0U;k<52U;++k){double complex z=symbols[offset*width+k]*rotation;uint32_t bit=k*2U;
            header[bit/8U]|=(uint8_t)((creal(z)<0.0)<<(7U-bit%8U));header[(bit+1U)/8U]|=(uint8_t)((cimag(z)<0.0)<<(7U-(bit+1U)%8U));}
        static const uint16_t words[6]={0xd1c2U,0xd1b4U,0xd10dU,0xa1c2U,0xa1b4U,0x7e7eU};static const uint8_t codes[6]={1U,2U,3U,4U,5U,15U};
        uint32_t word=header[0]*256U+header[1],payload=header[4]*256U+header[5];int legal=0;
        for(uint32_t k=0U;k<6U;++k)if(word==words[k]&&header[2]==32U+codes[k]&&payload>=12U&&payload<=4096U)legal=1;if(!legal)continue;
        uint32_t data_symbols=((payload+13U)*8U+2U*width-1U)/(2U*width),last=offset+data_symbols,cursor=start+last*stride;
        if(cursor+cp+nf>=n||last*width>used)continue;uint32_t length=(data_symbols+2U)*stride,edge=WRJ_MAX(1U,(uint32_t)lround(.012*length));
        for(uint32_t k=0U;k<nf;++k){uint32_t pos=(data_symbols+1U)*stride+cp+k+1U;double weight=1.0;
            if(pos>length-edge && edge>1U)weight=.5*(1.0-cos(WRJ_PI*((double)length-pos)/(edge-1U)));
            repaired[k]=x[cursor+cp+k]*cexp(-I*2.0*WRJ_PI*frequency*(cursor+cp+k)/fs)/WRJ_MAX(weight,1e-6);}
        for(uint32_t k=0U;k<cp;++k){uint32_t pos=(data_symbols+1U)*stride+k+1U;double weight=1.0;
            if(pos>length-edge && edge>1U)weight=.5*(1.0-cos(WRJ_PI*((double)length-pos)/(edge-1U)));
            prefix[k]=x[cursor+k]*cexp(-I*2.0*WRJ_PI*frequency*(cursor+k)/fs)/WRJ_MAX(weight,1e-6);}
        for(uint32_t k=16U;k<cp;++k)repaired[nf-cp+k]=prefix[k];(void)wrj_dft_complex(repaired,nf,0);
        for(uint32_t k=0U;k<width;++k)spectrum[k]=repaired[(first+k+nf/2U)%nf]/sqrt(nf)/channel[k];double phase=cpe(spectrum,width);
        double complex saved[2048];memcpy(saved,symbols+(last-1U)*width,width*sizeof(*saved));
        for(uint32_t local=0U;local<4U;++local){const double complex rotation=cexp(-I*(phase+local*WRJ_PI/2.0));for(uint32_t k=0U;k<width;++k)symbols[(last-1U)*width+k]=spectrum[k]*rotation;
            uint32_t before=out->count;if(wrj_symbols_to_records(symbols,used,record,out)){for(uint32_t k=before;k<out->count;++k)out->records[k].window_repaired=1U;return 1;}}
        memcpy(symbols+(last-1U)*width,saved,width*sizeof(*saved));
    }
    return 0;
}
static int adaptive_symbols(const double complex *x,uint32_t n,uint32_t start,uint32_t nf,uint32_t cp,double frequency,double fs,uint32_t first,uint32_t width,
    const double complex *initial,const wrj_rx_record_t *record,wrj_parse_result_t *out)
{
    uint32_t stride=nf+cp,limit=WRJ_MIN((n-start)/stride,WRJ_MAX(32U,(32872U+2U*width-1U)/(2U*width)+2U));
    double complex *symbols=calloc(limit*width,sizeof(*symbols));if(!symbols)return 0;double complex channel[2048],block[2048],z[2048],decision[2048],estimate[2048];
    double weights[2048],sorted[2048];uint8_t reliable[2048];int found=0;
    double complex *fft_blocks=calloc(limit*nf,sizeof(*fft_blocks));
    if(!fft_blocks){free(symbols);return 0;}
    for(uint32_t symbol=1U;symbol<limit;++symbol)
        if(!spectrum_at(x,n,start+symbol*stride,nf,cp,frequency,fs,fft_blocks+symbol*nf))break;
    for(uint32_t model=1U;model<=3U;++model){memcpy(channel,initial,width*sizeof(*channel));if(model>=2U)denoise_channel(channel,nf,first,width);
        double previous=0.0,velocity=0.0;uint32_t used=0U;
        for(uint32_t symbol=1U;symbol<limit;++symbol){memcpy(block,fft_blocks+symbol*nf,nf*sizeof(*block));
            for(uint32_t k=0U;k<width;++k)weights[k]=sorted[k]=energy(channel[k]);double cap=percentile(sorted,width,.8,1),threshold;
            double complex fourth=0.0;for(uint32_t k=0U;k<width;++k){weights[k]=WRJ_MIN(weights[k],cap);z[k]=block[first+k]/channel[k];double complex unit=z[k]/WRJ_MAX(cabs(z[k]),2.2204460492503131e-16);fourth+=unit*unit*unit*unit*weights[k];}
            double phase=carg(-fourth)/4.0,prediction=previous+velocity;phase+=round((prediction-phase)/(WRJ_PI/2.0))*(WRJ_PI/2.0);
            if(symbol>1U)velocity=.75*velocity+.25*(phase-previous);previous=phase;double linear=0.0;
            if(model==3U){double complex pair=0.0;for(uint32_t k=1U;k<width;++k){double complex a=z[k]/WRJ_MAX(cabs(z[k]),2.2204460492503131e-16),b=z[k-1U]/WRJ_MAX(cabs(z[k-1U]),2.2204460492503131e-16);
                    a*=a;a*=a;b*=b;b*=b;pair+=a*conj(b)*sqrt(weights[k]*weights[k-1U]);}linear=WRJ_CLAMP(carg(pair)/4.0,-.025,.025);fourth=0.0;
                for(uint32_t k=0U;k<width;++k){double complex unit=z[k]/WRJ_MAX(cabs(z[k]),2.2204460492503131e-16)*cexp(-I*linear*((double)k-width/2U));fourth+=unit*unit*unit*unit*weights[k];}
                double value=carg(-fourth)/4.0;phase=value+round((phase-value)/(WRJ_PI/2.0))*(WRJ_PI/2.0);}
            for(uint32_t k=0U;k<width;++k)sorted[k]=cabs(channel[k]);threshold=percentile(sorted,width,.2,1);uint32_t accepted=0U;double complex residual=0.0;
            for(uint32_t k=0U;k<width;++k){z[k]*=cexp(-I*(phase+linear*((double)k-width/2U)));decision[k]=((creal(z[k])<0.0?-1.0:1.0)+I*(cimag(z[k])<0.0?-1.0:1.0))/sqrt(2.0);
                double confidence=WRJ_MIN(fabs(creal(z[k])),fabs(cimag(z[k])))/WRJ_MAX(cabs(z[k]),2.2204460492503131e-16);
                reliable[k]=(uint8_t)(confidence>.3 && cabs(channel[k])>threshold);
                if(reliable[k]){++accepted;residual+=z[k]*conj(decision[k])*weights[k];}}
            if(accepted>width/3U){double delta=carg(residual);phase+=delta;const double complex rotation=cexp(-I*delta);for(uint32_t k=0U;k<width;++k){z[k]*=rotation;
                    estimate[k]=reliable[k]?block[first+k]/(decision[k]*cexp(I*(phase+linear*((double)k-width/2U)))):channel[k];}
                double current=slope(channel,width);double complex flat[2048],mean[2048];for(uint32_t k=0U;k<width;++k)flat[k]=estimate[k]*cexp(-I*current*k);mean_complex(flat,mean,width,11U);
                double gain=model==3U?.35:.1;for(uint32_t k=0U;k<width;++k)channel[k]=(1.0-gain)*channel[k]+gain*mean[k]*cexp(I*current*k);}
            memcpy(symbols+used,z,width*sizeof(*symbols));used+=width;
        }
        if(wrj_symbols_to_records_soft(symbols,used,record,out)){found=1;break;}
        if(out->event_count && tail_restore(x,n,start,nf,cp,frequency,fs,first,width,channel,symbols,used,record,out)){found=1;break;}
    }
    free(fft_blocks);free(symbols);return found;
}
static int refine_local(const double complex *x,uint32_t n,uint32_t start,uint32_t nf,uint32_t cp,
    double frequency,double fs,uint32_t first,uint32_t width,uint32_t root,
    const wrj_rx_record_t *record,wrj_parse_result_t *out)
{
    const uint32_t stride=nf+cp,skip=WRJ_MIN(16U,cp/4U);
    double complex sum=0.0;uint32_t accepted=0U;
    for(uint32_t symbol=0U;symbol<6U && start+(symbol+1U)*stride<=n;++symbol){
        double complex cross=0.0;double ea=0.0,eb=0.0;
        for(uint32_t k=skip;k<cp;++k){uint32_t pos=start+symbol*stride+k;
            double complex a=x[pos],b=x[pos+nf]*cexp(-I*2.0*WRJ_PI*frequency*nf/fs);
            cross+=b*conj(a);ea+=energy(a);eb+=energy(b);}
        double coherence=cabs(cross)/sqrt(ea*eb+2.2204460492503131e-16);
        if(coherence>.6){sum+=cross/WRJ_MAX(cabs(cross),2.2204460492503131e-16)*pow(coherence,4.0);++accepted;}
    }
    double residual=accepted>=2U?carg(sum)*fs/(2.0*WRJ_PI*nf):0.0;
    if(fabs(residual)>.45*fs/nf)return 0;
    for(uint32_t pilot=0U;pilot<2U;++pilot){
        uint32_t local_start=start+pilot*stride,pilot_root=root;
        double complex spectrum[2048],channel[2048],preview[4096];
        if(!spectrum_at(x,n,local_start,nf,cp,frequency+residual,fs,spectrum))continue;
        if(pilot){double complex curve=0.0,previous=0.0;
            for(uint32_t k=1U;k<width;++k){double complex delta=spectrum[first+k]*conj(spectrum[first+k-1U]);
                if(k>=2U)curve+=delta*conj(previous);previous=delta;}
            int64_t r=llround(-carg(curve)*width/(2.0*WRJ_PI));r%=(int64_t)width;if(r<0)r+=width;
            pilot_root=root==24U?42U:(uint32_t)r;
            if(pilot_root<1U || pilot_root>100U)continue;
        }
        for(uint32_t k=0U;k<width;++k)channel[k]=spectrum[first+k]/zc(pilot_root,width,k);
        if(channel_score(channel,width,slope(channel,width))<.85)continue;
        smooth_channel(channel,width,7U);uint32_t used=0U;
        for(uint32_t symbol=1U;symbol<=2U;++symbol){
            if(!spectrum_at(x,n,local_start+symbol*stride,nf,cp,frequency+residual,fs,spectrum))break;
            double complex z[2048];for(uint32_t k=0U;k<width;++k)z[k]=spectrum[first+k]/channel[k];
            double phase=cpe(z,width);const double complex rotation=cexp(-I*phase);for(uint32_t k=0U;k<width;++k)preview[used++]=z[k]*rotation;
        }
        if(!header_present(preview,used,width))continue;
        wrj_rx_record_t local_record=*record;local_record.start0=local_start;local_record.cfo_hz=frequency+residual;
        if(adaptive_symbols(x,n,local_start,nf,cp,frequency+residual,fs,first,width,channel,&local_record,out))return 1;
        if(tail_restore(x,n,local_start,nf,cp,frequency+residual,fs,first,width,channel,preview,used,&local_record,out))return 1;
    }
    return 0;
}
int wrj_ofdm_receive(const double complex *x,uint32_t n,double fs,const wrj_rx_record_t *context,
    wrj_parse_result_t *out,uint32_t level,int primary_only)
{
    static const uint32_t combos[9][2]={{512U,48U},{512U,64U},{1024U,96U},{1024U,112U},{1024U,128U},
        {2048U,128U},{2048U,144U},{2048U,160U},{768U,53U}};
    static const uint32_t basic[6][2]={{512U,48U},{512U,64U},{1024U,96U},{1024U,128U},{2048U,144U},{2048U,160U}};
    const uint32_t before=out->count;uint32_t local_attempts=0U,order[9]={0U,1U,2U,3U,4U,5U,6U,7U,8U};double rank[9]={0.0};
    const clock_t begin=clock();
    wrj_soft_data_budget_reset(32U);
    const uint32_t grids=level>=3U?9U:6U;wrj_phy_peak_t peaks[768];
    if(level>=3U)for(uint32_t g=0U;g<grids;++g){const uint32_t m=wrj_phy_cp_peaks(x,n,combos[g][0],combos[g][1],peaks,8U,.55,0);
        for(uint32_t k=0U;k<WRJ_MIN(4U,m);++k)rank[g]+=peaks[k].score/WRJ_MIN(4U,m);}
    if(level>=3U)for(uint32_t g=1U;g<grids;++g){const uint32_t value=order[g];uint32_t k=g;
        while(k && rank[order[k-1U]]<rank[value]){order[k]=order[k-1U];--k;}order[k]=value;}
    double complex spectrum[2048],channel[2048],block[2048],following[2][2048];grid_t hypotheses[80];
    double complex *symbols=calloc(131072U,sizeof(*symbols));if(!symbols)return 0;
    wrj_phy_begin_ofdm_cache();active_metrics=out;fft_cache_next=0U;
    if(level>=3U && !primary_only)active_fft_cache=calloc(24U,sizeof(*active_fft_cache));
    for(uint32_t grid=0U;grid<(primary_only?1U:grids);++grid){const uint32_t g=order[grid],nf=level>=3U?combos[g][0]:basic[g][0],cp=level>=3U?combos[g][1]:basic[g][1],stride=nf+cp;
        const uint32_t m=level>=3U?wrj_phy_coarse_ofdm_peaks(x,n,fs,nf,cp,peaks,768U):wrj_phy_cp_peaks(x,n,nf,cp,peaks,96U,.55,0);
        out->stages.cp_peaks+=m;
        for(uint32_t h=0U;h<(primary_only?WRJ_MIN(64U,m):m);++h){
            if((double)(clock()-begin)/CLOCKS_PER_SEC>(primary_only?4.0:600.0))goto finish;
            const int basic_shifts[7]={0,-2,-4,-8,-16,-24,-32},deep_shifts[5]={0,-(int)stride,-2,-16,-48};
            const int *shifts=level>=3U?deep_shifts:basic_shifts;const uint32_t shifts_count=primary_only?1U:level>=3U?5U:7U;
            for(uint32_t ts=0U;ts<shifts_count;++ts){const int64_t trial=(int64_t)peaks[h].start+shifts[ts];
                if(trial<0 || trial+3U*stride>n)continue;const uint32_t start=(uint32_t)trial;
                double hz=peaks[h].phase*fs/(2.0*WRJ_PI*nf);
                if(!spectrum_at(x,n,start,nf,cp,hz,fs,spectrum))continue;
                uint32_t nh=0U;
                if(level>=3U){(void)spectrum_at(x,n,start+stride,nf,cp,hz,fs,following[0]);
                    (void)spectrum_at(x,n,start+2U*stride,nf,cp,hz,fs,following[1]);
                    nh=engineering_grid(spectrum,nf,following[0],following[1],hypotheses);
                    if(!primary_only)nh+=joint_grid(spectrum,nf,following[0],following[1],hypotheses+nh);
                    if(!primary_only || !nh)nh+=single_grid(spectrum,nf,hypotheses+nh);
                }else nh=compatible_grid(spectrum,nf,hypotheses);
                if(primary_only && nh){uint32_t best=0U;for(uint32_t j=1U;j<nh;++j)if(hypotheses[j].rank>hypotheses[best].rank)best=j;
                    hypotheses[0]=hypotheses[best];nh=1U;}
                for(uint32_t p=0U;p<nh;++p){grid_t hypothesis=hypotheses[p];const uint32_t width=hypothesis.width;
                    ++out->total_hypotheses;
                    int duplicate=0;
                    if(level>=3U && !primary_only)for(uint32_t prior=0U;prior<p;++prior){const grid_t *v=hypotheses+prior;
                        if(v->first==hypothesis.first && v->width==width && v->root==hypothesis.root &&
                            (v->rank>=2.0)==(hypothesis.rank>=2.0)){duplicate=1;break;}}
                    if(duplicate){++out->duplicates_skipped;continue;}++out->unique_hypotheses;
                    const int64_t centered=(int64_t)(nf/2U)-(int64_t)(width/2U),integer=(int64_t)hypothesis.first-centered;
                    const double frequency=hz+integer*fs/nf;
                    if(!spectrum_at(x,n,start,nf,cp,frequency,fs,spectrum))continue;
                    if(centered<0 || centered+width>nf)continue;
                    for(uint32_t k=0U;k<width;++k)channel[k]=spectrum[centered+k]/zc(hypothesis.root,width,k);
                    const double coherence=channel_score(channel,width,slope(channel,width));
                    if(level>=3U && coherence<.75 && hypothesis.rank<2.0)continue;
                    if(level>=3U)smooth_channel(channel,width,7U);
                    ++out->stages.sync_success;++out->stages.cfo_success;
                    const uint32_t symbol_limit=WRJ_MAX(32U,(32872U+2U*width-1U)/(2U*width)+2U),available=(n-start)/stride;
                    uint32_t used=0U;double tracked=0.0;
                    for(uint32_t k=1U;k<WRJ_MIN(symbol_limit,available);++k){
                        if(!spectrum_at(x,n,start+k*stride,nf,cp,frequency,fs,block))break;
                        double complex equalized[2048];for(uint32_t j=0U;j<width;++j)equalized[j]=block[centered+j]/channel[j];
                        const double phase=cpe(equalized,width);tracked=phase+round((tracked-phase)/(WRJ_PI/2.0))*(WRJ_PI/2.0);
                        const double complex tracked_rotation=cexp(-I*tracked);
                        for(uint32_t j=0U;j<width && used<131072U;++j)symbols[used++]=equalized[j]*tracked_rotation;
                        if(k==2U && !header_present(symbols,used,width))break;
                    }
                    out->stages.symbol_success+=(uint32_t)(used>0U);wrj_rx_record_t record=*context;
                    record.start0=start;record.nfft=nf;record.cp=cp;record.active=width;record.cfo_hz=frequency;
                    uint32_t old=out->count;out->event_count=0U;out->incomplete_count=0U;
                    int found=(!primary_only && level>=3U ? wrj_symbols_to_records_soft(symbols,used,&record,out):
                        wrj_symbols_to_records(symbols,used,&record,out))>0;
                    if(!found && !primary_only && level>=3U && out->incomplete_count){
                        const uint32_t current_end=WRJ_MIN(n,start+symbol_limit*stride);uint32_t extended=current_end;
                        for(uint32_t j=0U;j<out->incomplete_count;++j){wrj_rx_record_t identity=record;
                            identity.byte_count=11U;memcpy(identity.bytes,out->incomplete_events[j].header,11U);uint32_t wanted=current_end;
                            if(wrj_received_window_end(n,current_end,&identity,out->incomplete_events[j].header,
                                out->incomplete_events[j].bit_start0,&wanted))extended=WRJ_MAX(extended,wanted);}
                        if(extended>current_end){used=0U;tracked=0.0;
                            const uint32_t blocks=(extended-start)/stride;
                            for(uint32_t k=1U;k<blocks;++k){
                                if(!spectrum_at(x,n,start+k*stride,nf,cp,frequency,fs,block))break;
                                double complex equalized[2048];for(uint32_t j=0U;j<width;++j)equalized[j]=block[centered+j]/channel[j];
                                const double phase=cpe(equalized,width);tracked=phase+round((tracked-phase)/(WRJ_PI/2.0))*(WRJ_PI/2.0);
                                const double complex rotation=cexp(-I*tracked);
                                for(uint32_t j=0U;j<width && used<131072U;++j)symbols[used++]=equalized[j]*rotation;
                            }
                            out->event_count=0U;out->incomplete_count=0U;
                            found=wrj_symbols_to_records_soft(symbols,used,&record,out)>0;
                        }
                    }
                    const int header_evidence=out->event_count>0U;
                    if(!found && !primary_only && level>=3U && out->event_count)
                        found=adaptive_symbols(x,n,start,nf,cp,frequency,fs,(uint32_t)centered,width,channel,&record,out);
                    if(!found && !primary_only && level>=2U)
                        found=tail_restore(x,n,start,nf,cp,frequency,fs,(uint32_t)centered,width,channel,symbols,used,&record,out);
                    if(!found && !primary_only && level>=3U && local_attempts<32U &&
                        ((coherence>.90 && header_evidence) || hypothesis.rank>=2.0)){
                        ++local_attempts;found=refine_local(x,n,start,nf,cp,frequency,fs,(uint32_t)centered,width,hypothesis.root,&record,out);
                    }
                    if(found && out->count>=before+((level==1U || primary_only)?2U:1U))goto finish;
                    (void)old;
                }
            }
        }
    }
finish:wrj_phy_end_ofdm_cache();free(active_fft_cache);active_fft_cache=NULL;active_metrics=NULL;free(symbols);return (int)(out->count-before);
}
