#include "wrj_e34_module3.h"
#include "wrj_io.h"
#include <stdlib.h>

int main(void)
{
    const uint32_t count = 65536U;
    m3_config_t config;
    m3_workspace_t workspace;
    wrj_cf32_t *iq = calloc(count, sizeof(*iq));
    wrj_sync_result_t result;
    wrj_candidate_t candidate;
    uint32_t k;
    double energy = 0.0;
    double active = 0.0, quiet = 0.0;
    int ok = iq != NULL;
    {
        FILE *file = fopen("m3_test_exact_capacity.cf32", "wb");
        const wrj_cf32_t values[2] = {{1.0f,2.0f},{3.0f,4.0f}};
        wrj_cf32_t loaded[2];
        uint32_t loaded_count = 0U;
        if (file == NULL) { free(iq); return 1; }
        ok &= fwrite(values,sizeof(values[0]),2U,file)==2U;
        ok &= fclose(file)==0;
        ok &= wrj_read_cf32("m3_test_exact_capacity.cf32",loaded,2U,&loaded_count)==WRJ_OK;
        ok &= loaded_count==2U && memcmp(values,loaded,sizeof(values))==0;
        ok &= wrj_read_cf32("m3_test_exact_capacity.cf32",loaded,1U,&loaded_count)==WRJ_ERR_CAPACITY;
        file = fopen("m3_test_exact_capacity.cf32", "wb");
        if (file == NULL) { free(iq); return 1; }
        ok &= fwrite(values,1U,5U,file)==5U;
        ok &= fclose(file)==0;
        ok &= wrj_read_cf32("m3_test_exact_capacity.cf32",loaded,2U,&loaded_count)==WRJ_ERR_DATA;
        ok &= remove("m3_test_exact_capacity.cf32")==0;
    }
    wrj_module3_fast_config(&config, count);
    ok &= config.enable_integer_cfo_search == 0U && config.enable_profile_retry == 0U &&
        config.enable_deep_receiver == 0U;
    if (!ok || m3_workspace_init(&workspace, &config) != WRJ_OK) { free(iq); return 1; }
    /* Sparse burst plus small capture noise. A median-only clip erases its contrast. */
    for (k=0U;k<count;++k) {
        const float amplitude = k>=30000U && k<30300U ? 10.0f : 0.01f;
        iq[k].re = amplitude*(float)cos(0.21*(double)k);
        iq[k].im = amplitude*(float)sin(0.21*(double)k);
    }
    iq[10].re = NAN;
    ok &= m3_preprocess_iq(iq,count,&workspace) == WRJ_OK;
    for(k=0U;k<count;++k) {
        double power = (double)iq[k].re*iq[k].re+(double)iq[k].im*iq[k].im;
        ok &= isfinite(power) != 0;
        energy += power;
        if(k>=30000U && k<30300U) active+=power; else quiet+=power;
    }
    ok &= fabs(energy/count-1.0)<1e-5;
    ok &= (active/300.0)/(quiet/(count-300U))>1e5;
    memset(&candidate,0,sizeof(candidate)); memset(&result,0,sizeof(result));
    ok &= wrj_module3_deep(&candidate,iq,count,1U,&workspace,&result)==WRJ_ERR_ARGUMENT;
    ok &= result.sync_accepted==0U && result.compensated_iq==NULL;
    ok &= wrj_module3_deep(&candidate,iq,count,3U,&workspace,&result)==WRJ_ERR_ARGUMENT;
    m3_workspace_release(&workspace); free(iq);
    if (!ok) { fprintf(stderr,"Module3 frontend regression failed\n"); return 1; }
    printf("Sparse-burst preservation, normalization, finite input, FAST isolation, DEEP input validation passed\n");
    return 0;
}
