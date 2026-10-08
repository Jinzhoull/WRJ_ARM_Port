#include "wrj_ble_e34.h"
#include "wrj_dsp64.h"
#include <complex.h>
#include <stdlib.h>
#define EPS 2.2204460492503131e-16
typedef struct {uint32_t start;double value;} peak_t;
typedef wrj_ble_packet_evidence_t packet_t;
static int order_double(const void *a,const void *b){double x=*(const double *)a,y=*(const double *)b;return(x>y)-(x<y);}
static int order_peak(const void *a,const void *b){const peak_t *x=a,*y=b;return x->value==y->value?(x->start>y->start)-(x->start<y->start):(x->value<y->value)-(x->value>y->value);}
static double median(double *x,uint32_t n){qsort(x,n,sizeof(*x),order_double);return n&1U?x[n/2U]:.5*(x[n/2U-1U]+x[n/2U]);}
static void mean(const double *x,double *out,uint32_t n,uint32_t w){uint32_t a=0U,b=0U;double sum=0.0;for(uint32_t k=0U;k<n;++k){uint32_t left=k>w/2U?k-w/2U:0U,right=WRJ_MIN(n,k+(w-1U)/2U+1U);while(b<right)sum+=x[b++];while(a<left)sum-=x[a++];out[k]=sum/(b-a);}}
static uint32_t crc24(const uint8_t *bits){uint32_t crc=0x555555U;for(uint32_t k=0U;k<312U;++k){uint32_t feedback=((crc>>23U)&1U)^bits[k];crc=(crc<<1U)&0xffffffU;if(feedback)crc^=0x65bU;}return crc;}
static void whiten_mask(uint8_t *out){uint32_t lfsr=38U|64U;for(uint32_t k=0U;k<360U;++k){out[k]=(uint8_t)(lfsr&1U);if(lfsr&1U)lfsr^=0x88U;lfsr>>=1U;}}
static int padded(const uint8_t *x,int identifier){uint32_t n=0U;while(n<20U && x[n]){uint8_t c=x[n++];if(c<32U || c>126U)return 0;if(identifier && !((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_'))return 0;}for(uint32_t k=n;k<20U;++k)if(x[k])return 0;return n>=(identifier?3U:1U);}
static uint32_t le(const uint8_t *x,uint32_t n){uint32_t v=0U;for(uint32_t k=0U;k<n;++k)v|=(uint32_t)x[k]<<(8U*k);return v;}
static int plausible(const uint8_t *p){const uint8_t *m=p+14U;uint32_t type=m[0]>>4U;if(type==0U)return padded(m+2U,1);if(type!=1U)return 1;int32_t lat=(int32_t)le(m+5U,4U),lon=(int32_t)le(m+9U,4U);double pressure=le(m+13U,2U)*.5-1000.0,alt=le(m+15U,2U)*.5-1000.0,speed=(m[1]&1U)?63.75+.75*m[3]:.25*m[3],heading=m[2]+180.0*((m[1]>>1U)&1U);return fabs(lat/1e7)<=90.0&&fabs(lon/1e7)<=180.0&&heading<=360.0&&le(m+15U,2U)>0U&&fabs(pressure-alt)<=30.0&&alt>=-500.0&&alt<=10000.0&&speed<=250.0;}
static void parse(packet_t *p,const uint8_t *bits){memset(p->pdu,0,sizeof(p->pdu));for(uint32_t k=0U;k<312U;++k)p->pdu[k/8U]|=(uint8_t)(bits[k]<<(k%8U));p->crc=0U;for(uint32_t k=312U;k<336U;++k)p->crc=(p->crc<<1U)|bits[k];p->valid=(uint8_t)(p->pdu[1]==37U && crc24(bits)==p->crc);p->pdu_found=(uint8_t)((p->pdu[1]&63U)>=6U && (p->pdu[1]&63U)<=40U);const uint8_t *m=p->pdu+14U;const uint32_t type=m[0]>>4U;p->found=(uint8_t)(p->pdu[8]==30U&&p->pdu[9]==0x16U&&p->pdu[10]==0xfaU&&p->pdu[11]==0xffU&&p->pdu[12]==13U&&(m[0]&15U)==2U&&(type==0U||type==1U||type==4U||type==5U));if(p->found){if(type==0U)p->found=(uint8_t)(m[22]==0U&&m[23]==0U&&m[24]==0U&&((m[1]>>4U!=1U&&m[1]>>4U!=2U)||padded(m+2U,0)));else if(type==5U)p->found=(uint8_t)(m[1]==0U&&m[22]==0U&&m[23]==0U&&m[24]==0U&&padded(m+2U,0));else p->found=(uint8_t)(m[24]==0U);}p->valid&=p->found;}
/* The frozen payload list model keeps all 24 received CRC bits immutable.
 * Public header constants are hypotheses, admitted only by original CRC. */
static int list_search(uint8_t *bits,uint32_t syndrome,const uint32_t *effects,const uint32_t *positions,uint32_t n,uint32_t depth,uint32_t first,packet_t *p){if(!depth){if(syndrome)return 0;parse(p,bits);return p->valid&&plausible(p->pdu);}for(uint32_t k=first;k+depth<=n;++k){uint32_t q=positions[k];bits[q]^=1U;if(list_search(bits,syndrome^effects[q],effects,positions,n,depth-1U,k+1U,p))return 1;bits[q]^=1U;}return 0;}
static int soft_recover(packet_t *p,const uint8_t *fixed,const uint8_t *known){uint8_t base[336];memcpy(base,p->raw,336U);for(uint32_t k=0U;k<312U;++k)if(known[k])base[k]=fixed[k];packet_t trial=*p;parse(&trial,base);if(trial.valid&&plausible(trial.pdu)){*p=trial;p->recovered=1U;return 1;}uint32_t pos[312],used=0U;for(uint32_t k=0U;k<312U;++k)if(!known[k])pos[used++]=k;for(uint32_t k=1U;k<used;++k){uint32_t q=pos[k],j=k;while(j&&fabs(p->soft[pos[j-1U]])>fabs(p->soft[q])){pos[j]=pos[j-1U];--j;}pos[j]=q;}static uint32_t effects[312];static int initialized=0;if(!initialized){uint8_t zero[312]={0U};uint32_t origin=crc24(zero);for(uint32_t k=0U;k<312U;++k){zero[k]=1U;effects[k]=origin^crc24(zero);zero[k]=0U;}initialized=1;}uint32_t received=0U;for(uint32_t k=312U;k<336U;++k)received=(received<<1U)|base[k];uint32_t syndrome=crc24(base)^received;static const uint32_t limits[6]={24U,20U,16U,12U,10U,8U};for(uint32_t depth=1U;depth<=6U;++depth){trial=*p;if(list_search(base,syndrome,effects,pos,WRJ_MIN(used,limits[depth-1U]),depth,0U,&trial)){*p=trial;p->recovered=1U;return 1;}}return 0;}
/* Group adjacent received starts before any CRC cache deduplication. */
static uint32_t deduplicate(packet_t *packets,uint32_t count,uint32_t sps)
{
    for(uint32_t k=1U;k<count;++k){packet_t value=packets[k];uint32_t j=k;
        while(j && packets[j-1U].start>value.start){packets[j]=packets[j-1U];--j;}packets[j]=value;}
    uint32_t used=0U;
    for(uint32_t a=0U;a<count;){uint32_t b=a+1U,best=a;
        while(b<count && packets[b].start-packets[b-1U].start<=3U*sps){
            if(packets[b].rank>packets[best].rank)best=b;++b;}
        packets[used++]=packets[best];a=b;}
    return used;
}
static uint32_t repeat_combine(packet_t *packets,uint32_t count,uint32_t capacity,
    const uint8_t *fixed,const uint8_t *known)
{
    static const uint8_t types[4]={0U,1U,4U,5U};
    uint8_t classified[48];double scores[48];uint32_t original=count;
    for(uint32_t k=0U;k<original;++k){classified[k]=255U;scores[k]=-1e30;
        if(packets[k].structure<.12)continue;
        double norm=0.0,best=-1e30;uint32_t winner=0U;
        for(uint32_t bit=0U;bit<8U;++bit)norm+=packets[k].soft[112U+bit]*packets[k].soft[112U+bit];
        for(uint32_t type=0U;type<4U;++type){double score=0.0;uint8_t byte=16U*types[type]+2U;
            for(uint32_t bit=0U;bit<8U;++bit)score+=(((byte>>bit)&1U)?1.0:-1.0)*packets[k].soft[112U+bit];
            score/=sqrt(8.0)*(sqrt(norm)+EPS);if(score>best){best=score;winner=type;}}
        if(best>=.05){classified[k]=types[winner];scores[k]=2.0*packets[k].structure+packets[k].score+.5*packets[k].pdu_found+.75*best;}
    }
    for(uint32_t type=0U;type<4U;++type){uint32_t group[48],used=0U;
        for(uint32_t k=0U;k<original;++k)if(classified[k]==types[type])group[used++]=k;
        for(uint32_t k=1U;k<used;++k){uint32_t v=group[k],j=k;while(j && scores[group[j-1U]]<scores[v]){group[j]=group[j-1U];--j;}group[j]=v;}
        used=WRJ_MIN(5U,used);
        for(uint32_t a=0U;a<used;++a)for(uint32_t b=a+1U;b<used;++b)for(uint32_t direction=0U;direction<2U;++direction){
            const packet_t *pa=packets+group[direction?b:a],*pb=packets+group[direction?a:b];packet_t p=*pa;
            double scale[336];for(uint32_t k=0U;k<336U;++k)scale[k]=fabs(pa->soft[k]);double sa=median(scale,336U)+EPS;
            for(uint32_t k=0U;k<336U;++k)scale[k]=fabs(pb->soft[k]);double sb=median(scale,336U)+EPS;
            for(uint32_t k=0U;k<336U;++k){uint32_t byte=k/8U;int stable=byte>=14U && byte<39U;
                if(types[type]==1U && (byte==35U || byte==36U))stable=0;
                if(types[type]==4U && byte>=34U && byte<=37U)stable=0;
                p.soft[k]=pa->soft[k]/sa;if(stable || known[k])p.soft[k]+=pb->soft[k]/sb;
                if(known[k])p.soft[k]=4.0*(2.0*fixed[k]-1.0);
                if(k>=112U && k<120U)p.soft[k]=4.0*((((16U*types[type]+2U)>>(k-112U))&1U)?1.0:-1.0);
                p.raw[k]=(uint8_t)(p.soft[k]>0.0);
            }
            parse(&p,p.raw);if(!p.valid)soft_recover(&p,fixed,known);
            if(!p.valid)continue;
            /* CRC suffix is from pa alone; no combining or correction. */
            p.score=.5*(pa->score+pb->score);p.structure=.5*(pa->structure+pb->structure);p.recovered=1U;
            p.rank=20.0*p.valid+5.0*p.found+.5*p.pdu_found+2.0*p.structure+p.score;
            if(count<capacity)packets[count++]=p;
        }
    }
    return count;
}
static double *correlate(const double *x,uint32_t n,const double *ref,uint32_t len,int energy_weight){if(n<len)return NULL;uint32_t nf=1U;while(nf<n+len-1U)nf<<=1U;double *re=calloc(nf,sizeof(double)),*im=calloc(nf,sizeof(double)),*rr=calloc(nf,sizeof(double)),*ri=calloc(nf,sizeof(double));uint32_t valid=n-len+1U;double *out=calloc(valid,sizeof(double)),*energies=calloc(valid,sizeof(double));if(!re||!im||!rr||!ri||!out||!energies){free(re);free(im);free(rr);free(ri);free(out);free(energies);return NULL;}memcpy(re,x,n*sizeof(double));double refpower=0.0,sum=0.0;for(uint32_t k=0U;k<len;++k){rr[k]=ref[len-1U-k];refpower+=ref[k]*ref[k];sum+=x[k]*x[k];}wrj_fft64(re,im,nf,0);wrj_fft64(rr,ri,nf,0);for(uint32_t k=0U;k<nf;++k){double a=re[k],b=im[k];re[k]=a*rr[k]-b*ri[k];im[k]=a*ri[k]+b*rr[k];}wrj_fft64(re,im,nf,1);for(uint32_t k=0U;k<valid;++k){energies[k]=WRJ_MAX(0.0,sum);out[k]=fabs(re[k+len-1U])/sqrt(WRJ_MAX(0.0,sum)*refpower+EPS);if(k+len<n)sum+=x[k+len]*x[k+len]-x[k]*x[k];}if(energy_weight){double center=median(energies,valid);sum=0.0;for(uint32_t k=0U;k<len;++k)sum+=x[k]*x[k];for(uint32_t k=0U;k<valid;++k){double db=10.0*log10((WRJ_MAX(sum,0.0)+EPS)/(center+EPS));out[k]*=.35+.65*WRJ_CLAMP((db-.5)/5.5,0.0,1.0);if(k+len<n)sum+=x[k+len]*x[k+len]-x[k]*x[k];}}free(re);free(im);free(rr);free(ri);free(energies);return out;}
static double *template(const double *symbols,uint32_t bits,uint32_t sps){uint32_t len=bits*sps,span=WRJ_MAX(5U,2U*(uint32_t)floor(2.5*sps)+1U);double *ref=calloc(len,sizeof(double)),*g=calloc(span,sizeof(double));if(!ref||!g){free(ref);free(g);return NULL;}double sum=0.0;for(uint32_t k=0U;k<span;++k){double v=((double)k-span/2U)/(.38*sps);g[k]=exp(-.5*v*v);sum+=g[k];}for(uint32_t k=0U;k<span;++k)g[k]/=sum;for(uint32_t k=0U;k<len;++k)for(uint32_t j=0U;j<span;++j){int64_t pos=(int64_t)k+span/2U-j;if(pos>=0 && pos<len)ref[k]+=g[j]*symbols[pos/sps];}free(g);return ref;}
static uint32_t candidates(const double *metric,uint32_t n,uint32_t sep,int legacy,peak_t *out){peak_t *all=calloc(n,sizeof(*all));double *sorted=calloc(n,sizeof(double));if(!all||!sorted){free(all);free(sorted);return 0U;}memcpy(sorted,metric,n*sizeof(double));qsort(sorted,n,sizeof(double),order_double);double peak=sorted[n-1U],pos=.995*(n-1U);uint32_t low=(uint32_t)floor(pos);double q=sorted[low]+(pos-low)*(sorted[WRJ_MIN(low+1U,n-1U)]-sorted[low]);double threshold=legacy?WRJ_MAX(.10,.45*peak):WRJ_MAX(.055,WRJ_MIN(q,.30*peak));uint32_t used=0U,count=0U;for(uint32_t k=1U;k+1U<n;++k)if(metric[k]>=threshold&&(legacy||(metric[k]>metric[k-1U]&&metric[k]>=metric[k+1U])))all[used++]=(peak_t){k,metric[k]};qsort(all,used,sizeof(*all),order_peak);for(uint32_t k=0U;k<used&&(legacy?k<5000U:1)&&count<96U;++k){int separate=1;for(uint32_t j=0U;j<count;++j)if(llabs((int64_t)all[k].start-out[j].start)<sep)separate=0;if(separate)out[count++]=all[k];}if(!count){uint32_t k=0U;for(uint32_t j=1U;j<n;++j)if(metric[j]>metric[k])k=j;out[count++]=(peak_t){k,metric[k]};}free(all);free(sorted);return count;}
static packet_t demod(const double *prefix,uint32_t n,uint32_t start,uint32_t sps,const double *expected,const uint8_t *whitening,const uint8_t *fixed,const uint8_t *known){packet_t best;memset(&best,0,sizeof(best));best.rank=-1e30;static const double fractions[3]={.22,.32,.42};for(uint32_t w=0U;w<3U;++w){int half=(int)WRJ_MAX(1U,(uint32_t)lround(sps*fractions[w]));for(int shift=-(int)sps;shift<=(int)sps;++shift){int64_t first=(int64_t)start+shift+sps/2U,last=first+399U*sps;if(first-half<0||last+half>=n)continue;double samples[400],gain=0.0,dc=0.0;for(uint32_t k=0U;k<400U;++k){int64_t center=first+k*sps;samples[k]=(prefix[center+half+1]-prefix[center-half])/(2*half+1);}double sx=0.0,sy=0.0,sxy=0.0;for(uint32_t k=0U;k<40U;++k){sx+=expected[k];sy+=samples[k];sxy+=expected[k]*samples[k];}gain=(sxy-sx*sy/40.0)/(40.0-sx*sx/40.0);dc=(sy-gain*sx)/40.0;if(fabs(gain)<EPS)continue;for(uint32_t k=0U;k<400U;++k)samples[k]=(samples[k]-dc)/gain;double dot=0.0,norm=0.0;for(uint32_t k=0U;k<40U;++k){dot+=expected[k]*samples[k];norm+=samples[k]*samples[k];}double pre=dot/(sqrt(40.0*norm)+EPS);dot=norm=0.0;uint32_t count=0U;for(uint32_t k=0U;k<312U;++k)if(known[k]){double sign=(2.0*fixed[k]-1.0)*(1.0-2.0*whitening[k]);dot+=sign*samples[k+40U];norm+=samples[k+40U]*samples[k+40U];++count;}double structure=dot/(sqrt(count*norm)+EPS);if(pre<.08||structure<.12)continue;packet_t p;memset(&p,0,sizeof(p));p.start=start;p.timing=shift;for(uint32_t bit=0U;bit<360U;++bit)p.white_soft[bit]=samples[40U+bit];p.score=.68*pre+.32*structure;p.structure=structure;for(uint32_t k=0U;k<336U;++k){p.soft[k]=samples[k+40U]*(1.0-2.0*whitening[k]);p.raw[k]=(uint8_t)(p.soft[k]>0.0);}parse(&p,p.raw);if(!p.valid&&structure>=.58&&pre>=.12)soft_recover(&p,fixed,known);p.rank=20.0*p.valid+5.0*p.found+.5*p.pdu_found+2.0*structure+p.score;if(p.rank>best.rank)best=p;}}return best;}
wrj_status_t wrj_ble_decode_e34_ex(const wrj_cf32_t *iq,uint32_t count,double fs,m3_result_t *out,
    packet_t *evidence,uint32_t capacity,uint32_t *evidence_count)
{
    packet_t *packets=NULL;if(evidence_count)*evidence_count=0U;
    if(!iq||!out||count<512U)return WRJ_ERR_ARGUMENT;uint32_t n=count-1U,sps=WRJ_MAX(4U,(uint32_t)lround(fs/1e6));
    double *dphi=calloc(n,sizeof(double)),*branch=calloc(n,sizeof(double)),*smoothed=calloc(n,sizeof(double)),*prefix=calloc(n+1U,sizeof(double)),*scratch=calloc(n,sizeof(double));double complex *product=calloc(n,sizeof(*product));if(!dphi||!branch||!smoothed||!prefix||!scratch||!product)goto fail;
    uint32_t base=(uint32_t)lround(.25*sps);static const double multipliers[4]={.65,1.0,1.45,1.90},weights[4]={.16,.34,.30,.20};
    uint32_t lag_set[4],lag_count=0U;for(uint32_t j=0U;j<4U;++j){uint32_t lag=WRJ_MAX(1U,(uint32_t)lround(base*multipliers[j]));if(!lag_count || lag_set[lag_count-1U]!=lag)lag_set[lag_count++]=lag;}
    for(uint32_t j=0U;j<lag_count;++j){uint32_t lag=lag_set[j],len=count-lag,a=0U,b=0U;double complex sum=0.0;for(uint32_t k=0U;k<len;++k)product[k]=conj((double)iq[k].re+I*iq[k].im)*((double)iq[k+lag].re+I*iq[k+lag].im);for(uint32_t k=0U;k<len;++k){uint32_t left=k>lag/2U?k-lag/2U:0U,right=WRJ_MIN(len,k+(lag-1U)/2U+1U);while(b<right)sum+=product[b++];while(a<left)sum-=product[a++];branch[k]=carg(sum/(b-a))/lag;}uint32_t offset=(lag-1U)/2U;for(uint32_t k=0U;k<n;++k)dphi[k]+=(lag_count==4U?weights[j]:1.0/lag_count)*branch[k<offset?0U:WRJ_MIN(len-1U,k-offset)];}
    if(getenv("WRJ_BLE_DPHI_PROBE")){FILE *probe=fopen(getenv("WRJ_BLE_DPHI_PROBE"),"wb");if(probe){fwrite(dphi,sizeof(double),n,probe);fclose(probe);}}mean(dphi,smoothed,n,WRJ_MAX(1U,(uint32_t)lround(.06*sps)));memcpy(scratch,smoothed,n*sizeof(double));double center=median(scratch,n);for(uint32_t k=0U;k<n;++k)smoothed[k]-=center;
    uint8_t whitening[360],fixed[336]={0U},known[336]={0U};whiten_mask(whitening);static const uint8_t bytes[7]={0x42U,37U,30U,0x16U,0xfaU,0xffU,13U},positions[7]={0U,1U,8U,9U,10U,11U,12U};for(uint32_t j=0U;j<7U;++j)for(uint32_t k=0U;k<8U;++k){uint32_t q=8U*positions[j]+k;known[q]=1U;fixed[q]=(bytes[j]>>k)&1U;}
    double expected[40],public_symbols[144]={0.0};uint32_t aa=0x8e89bed6U;for(uint32_t k=0U;k<40U;++k){uint32_t bit=k<8U?(k&1U):(aa>>(k-8U))&1U;expected[k]=public_symbols[k]=2.0*bit-1.0;}for(uint32_t k=0U;k<104U;++k)if(known[k])public_symbols[k+40U]=.85*(2.0*fixed[k]-1.0)*(1.0-2.0*whitening[k]);
    double *ref=template(public_symbols,144U,sps);if(!ref)goto fail;double avg=0.0;for(uint32_t k=0U;k<144U*sps;++k)avg+=ref[k];avg/=144U*sps;for(uint32_t k=0U;k<144U*sps;++k)ref[k]-=avg;double *metric=correlate(smoothed,n,ref,144U*sps,1);free(ref);if(!metric)goto fail;
    if(getenv("WRJ_BLE_METRIC_PROBE")){FILE *probe=fopen(getenv("WRJ_BLE_METRIC_PROBE"),"wb");if(probe){fwrite(metric,sizeof(double),n-144U*sps+1U,probe);fclose(probe);}}peak_t chosen[192];uint32_t used=candidates(metric,n-144U*sps+1U,4U*sps,0,chosen);free(metric);
    /* The supplementary detector has its own detrended legacy metric. */
    mean(dphi,branch,n,WRJ_MAX(3U,(uint32_t)lround(.18*sps)));uint32_t window=8U*sps;double *moving=calloc(window+1U,sizeof(double));if(!moving)goto fail;for(uint32_t k=0U;k<n;++k){uint32_t a=k>window/2U?k-window/2U:0U,b=WRJ_MIN(n,k+(window-1U)/2U+1U);memcpy(moving,branch+a,(b-a)*sizeof(double));smoothed[k]=branch[k]-median(moving,b-a);}free(moving);ref=template(expected,40U,sps);metric=ref?correlate(smoothed,n,ref,40U*sps,1):NULL;free(ref);if(metric){peak_t extra[96];uint32_t m=candidates(metric,n-40U*sps+1U,4U*sps,1,extra);for(uint32_t k=0U;k<m;++k){int duplicate=0;for(uint32_t j=0U;j<used;++j)duplicate|=chosen[j].start==extra[k].start;if(!duplicate&&used<192U)chosen[used++]=extra[k];}free(metric);}qsort(chosen,used,sizeof(*chosen),order_peak);used=WRJ_MIN(used,48U);for(uint32_t k=1U;k<used;++k){peak_t value=chosen[k];uint32_t j=k;while(j&&chosen[j-1U].start>value.start){chosen[j]=chosen[j-1U];--j;}chosen[j]=value;}
    for(uint32_t k=0U;k<n;++k)prefix[k+1U]=prefix[k]+dphi[k];packets=calloc(256U,sizeof(*packets));if(!packets)goto fail;uint32_t packet_count=0U;out->ble_verified_packet_count=0U;
    for(uint32_t k=0U;k<used;++k){packet_t p=demod(prefix,n,chosen[k].start,sps,expected,whitening,fixed,known);
        if(p.pdu_found || p.structure>=.15)packets[packet_count++]=p;}
    packet_count=deduplicate(packets,packet_count,sps);
    packet_count=repeat_combine(packets,packet_count,256U,fixed,known);
    packet_count=deduplicate(packets,packet_count,sps);
    if(evidence && evidence_count){*evidence_count=WRJ_MIN(capacity,packet_count);memcpy(evidence,packets,*evidence_count*sizeof(*evidence));}
    for(uint32_t k=0U;k<packet_count;++k){packet_t *p=packets+k;if(getenv("WRJ_BLE_STAGE_PROBE"))fprintf(stderr,"BLE_PDU start=%u score=%.9g structure=%.9g valid=%u found=%u pdu=%02X%02X %02X%02X%02X%02X%02X type=%02X crc=%06X\n",p->start,p->score,p->structure,p->valid,p->found,p->pdu[0],p->pdu[1],p->pdu[8],p->pdu[9],p->pdu[10],p->pdu[11],p->pdu[12],p->pdu[14],p->crc);if(!p->valid)continue;uint32_t slot=out->ble_verified_packet_count;if(slot>=WRJ_MAX_PACKETS)break;int duplicate=0;for(uint32_t j=0U;j<slot;++j)if(memcmp(p->pdu,out->ble_verified_pdu[j],39U)==0)duplicate=1;if(duplicate)continue;memcpy(out->ble_verified_pdu[slot],p->pdu,39U);memcpy(out->ble_verified_raw_bits[slot],p->raw,336U);out->ble_verified_crc[slot]=p->crc;out->ble_verified_start[slot]=p->start;out->ble_verified_confidence[slot]=(float)p->score;++out->ble_verified_packet_count;}
    free(dphi);free(branch);free(smoothed);free(prefix);free(scratch);free(product);free(packets);return WRJ_OK;
fail:free(dphi);free(branch);free(smoothed);free(prefix);free(scratch);free(product);free(packets);return WRJ_ERR_MEMORY;
}

wrj_status_t wrj_ble_decode_e34(const wrj_cf32_t *iq,uint32_t count,double fs,m3_result_t *out)
{
    return wrj_ble_decode_e34_ex(iq,count,fs,out,NULL,0U,NULL);
}

static int protected_list(packet_t *packet,uint8_t *base,const uint8_t *protected,const uint32_t *limits)
{
    parse(packet,base);if(packet->valid && plausible(packet->pdu))return 1;
    static uint32_t effects[312];static int initialized=0;
    if(!initialized){uint8_t zero[312]={0U};uint32_t origin=crc24(zero);
        for(uint32_t k=0U;k<312U;++k){zero[k]=1U;effects[k]=origin^crc24(zero);zero[k]=0U;}initialized=1;}
    uint32_t positions[312],count=0U,received=0U;
    for(uint32_t k=0U;k<312U;++k)if(!protected[k])positions[count++]=k;
    for(uint32_t k=1U;k<count;++k){uint32_t q=positions[k],j=k;
        while(j && fabs(packet->soft[positions[j-1U]])>fabs(packet->soft[q])){positions[j]=positions[j-1U];--j;}positions[j]=q;}
    for(uint32_t k=312U;k<336U;++k)received=(received<<1U)|base[k];uint32_t syndrome=crc24(base)^received;
    for(uint32_t depth=1U;depth<=6U;++depth)if(list_search(base,syndrome,effects,positions,WRJ_MIN(count,limits[depth-1U]),depth,0U,packet))return 1;
    return 0;
}
static int try_gap_basic(packet_t *packet,const double *values,uint8_t counter,const uint8_t *fixed,const uint8_t *known)
{
    packet_t p=*packet;uint8_t base[336],protected[336];
    for(uint32_t k=0U;k<336U;++k){p.soft[k]=values[k];p.raw[k]=(uint8_t)(values[k]>0.0);}
    for(uint32_t k=0U;k<8U;++k){p.soft[104U+k]=3.0*((counter>>k&1U)?1.0:-1.0);p.soft[112U+k]=4.0*((2U>>k&1U)?1.0:-1.0);
        p.raw[104U+k]=(uint8_t)(p.soft[104U+k]>0.0);p.raw[112U+k]=(uint8_t)(p.soft[112U+k]>0.0);}
    parse(&p,p.raw);
    if(!p.valid)soft_recover(&p,fixed,known);
    if(p.valid && p.pdu[14]>>4U==0U && plausible(p.pdu)){*packet=p;return 1;}
    memcpy(base,p.raw,336U);memcpy(protected,known,336U);
    for(uint32_t k=0U;k<312U;++k)if(known[k])base[k]=fixed[k];
    for(uint32_t k=104U;k<120U;++k)protected[k]=1U;
    static const char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_";
    uint8_t selected[336];double best=-1e30;
    for(uint32_t length=3U;length<=20U;++length){uint8_t trial[336];memcpy(trial,base,336U);double score=0.0;
        for(uint32_t byte=0U;byte<20U;++byte){uint32_t position=(16U+byte)*8U;uint8_t winner=0U;
            if(byte<length){double maximum=-1e30;
                for(uint32_t option=0U;option<sizeof(alphabet)-1U;++option){double value=0.0;uint8_t ch=(uint8_t)alphabet[option];
                    for(uint32_t bit=0U;bit<8U;++bit)value+=(((ch>>bit)&1U)?1.0:-1.0)*p.soft[position+bit];
                    if(value>maximum){maximum=value;winner=ch;}}score+=maximum;
            }else for(uint32_t bit=0U;bit<8U;++bit)score-=p.soft[position+bit];
            for(uint32_t bit=0U;bit<8U;++bit)trial[position+bit]=(winner>>bit)&1U;
        }
        if(score>best){best=score;memcpy(selected,trial,336U);}
    }
    for(uint32_t k=128U;k<288U;++k)protected[k]=1U;
    static const uint32_t limits[6]={32U,28U,24U,20U,16U,12U};
    if(protected_list(&p,selected,protected,limits) && p.pdu[14]>>4U==0U && plausible(p.pdu)){*packet=p;return 1;}
    return 0;
}
static void cache_packet(const packet_t *packet,m3_result_t *out)
{
    if(!packet->valid || !packet->found)return;
    uint32_t slot=out->ble_verified_packet_count;if(slot>=WRJ_MAX_PACKETS)return;
    for(uint32_t j=0U;j<slot;++j)if(memcmp(out->ble_verified_pdu[j],packet->pdu,39U)==0)return;
    memcpy(out->ble_verified_pdu[slot],packet->pdu,39U);memcpy(out->ble_verified_raw_bits[slot],packet->raw,336U);
    out->ble_verified_crc[slot]=packet->crc;out->ble_verified_start[slot]=packet->start;
    out->ble_verified_confidence[slot]=(float)packet->score;++out->ble_verified_packet_count;
}
void wrj_ble_gap_recover_e34(packet_t *packets,uint32_t count,uint32_t sps,m3_result_t *out)
{
    if(!packets || !count || !out)return;
    packet_t *observed=calloc(count,sizeof(*observed));if(!observed)return;uint32_t valid_count=0U;int basic=0;
    for(uint32_t k=0U;k<count;++k)if(packets[k].valid){observed[valid_count++]=packets[k];basic|=packets[k].pdu[14]>>4U==0U;}
    valid_count=deduplicate(observed,valid_count,sps);
    uint8_t whitening[360],fixed[336]={0U},known[336]={0U};whiten_mask(whitening);
    static const uint8_t bytes[7]={0x42U,37U,30U,0x16U,0xfaU,0xffU,13U},positions[7]={0U,1U,8U,9U,10U,11U,12U};
    for(uint32_t j=0U;j<7U;++j)for(uint32_t bit=0U;bit<8U;++bit){uint32_t q=8U*positions[j]+bit;known[q]=1U;fixed[q]=(bytes[j]>>bit)&1U;}
    packet_t recovered[32];uint32_t recovered_count=0U;
    if(!basic)for(uint32_t pair=0U;pair+1U<valid_count;++pair){
        const packet_t *a=observed+pair,*b=observed+pair+1U;uint32_t step=(b->pdu[13]-a->pdu[13]+256U)%256U;
        double first=a->start+a->timing,last=b->start+b->timing;if(step<2U || step>4U || last<=first)continue;
        double period=(last-first)/step;
        for(uint32_t gap=1U;gap<step && recovered_count<32U;++gap){double target=first+gap*period;uint8_t counter=(a->pdu[13]+gap)%256U;
            uint32_t *eligible=calloc(count,sizeof(*eligible)),used=0U;if(!eligible)continue;
            for(uint32_t k=0U;k<count;++k)if(!packets[k].valid && fabs(packets[k].start+packets[k].timing-target)<=WRJ_MAX(3.0*sps,.45*period))eligible[used++]=k;
            for(uint32_t k=1U;k<used;++k){uint32_t v=eligible[k],j=k;double rank=packets[v].score+packets[v].structure;
                while(j && packets[eligible[j-1U]].score+packets[eligible[j-1U]].structure<rank){eligible[j]=eligible[j-1U];--j;}eligible[j]=v;}
            used=WRJ_MIN(18U,used);double bank[54][336],weights[54],quality[54];uint32_t bank_count=0U;
            for(uint32_t index=0U;index<used;++index)for(uint32_t bit_offset=0U;bit_offset<3U;++bit_offset){
                const packet_t *source=packets+eligible[index];double scale[336],soft[336];
                for(uint32_t k=0U;k<336U;++k)scale[k]=fabs(soft[k]=source->white_soft[k+bit_offset]*(1.0-2.0*whitening[k]));
                double norm=median(scale,336U)+EPS,sum=0.0;
                for(uint32_t k=0U;k<336U;++k){soft[k]/=norm;bank[bank_count][k]=soft[k];sum+=fabs(soft[k]);}
                weights[bank_count]=WRJ_MAX(.05,source->score+source->structure);quality[bank_count]=sum*weights[bank_count];++bank_count;
                packet_t trial=*source;if(try_gap_basic(&trial,soft,counter,fixed,known) && recovered_count<32U){trial.recovered=1U;recovered[recovered_count++]=trial;}
            }
            if(!recovered_count && bank_count>=2U && used){uint32_t order[54];for(uint32_t k=0U;k<bank_count;++k)order[k]=k;
                for(uint32_t k=1U;k<bank_count;++k){uint32_t v=order[k],j=k;while(j && quality[order[j-1U]]<quality[v]){order[j]=order[j-1U];--j;}order[j]=v;}
                static const uint32_t sizes[7]={2U,3U,4U,6U,8U,12U,18U};uint32_t previous=0U;
                for(uint32_t q=0U;q<7U;++q){uint32_t size=WRJ_MIN(bank_count,sizes[q]);if(size==previous)continue;previous=size;
                    double soft[336]={0.0},sum=0.0;for(uint32_t k=0U;k<size;++k){sum+=weights[order[k]];
                        for(uint32_t bit=0U;bit<336U;++bit)soft[bit]+=weights[order[k]]*bank[order[k]][bit];}
                    for(uint32_t bit=0U;bit<336U;++bit)soft[bit]/=sum;packet_t trial=packets[eligible[0]];
                    if(try_gap_basic(&trial,soft,counter,fixed,known)){trial.start=(uint32_t)lround(target);trial.timing=target-trial.start;trial.recovered=1U;recovered[recovered_count++]=trial;break;}
                }
            }
            free(eligible);
        }
    }
    out->ble_verified_packet_count=0U;
    for(uint32_t k=0U;k<valid_count;++k)cache_packet(observed+k,out);
    for(uint32_t k=0U;k<recovered_count;++k)cache_packet(recovered+k,out);
    free(observed);
}
