#include "wrj_e34_module3.h"
#include "wrj_ble_e34.h"
#include <stdlib.h>

void wrj_module3_fast_config(m3_config_t *config, uint32_t samples)
{
    m3_default_config(config);
    if (config == NULL) return;
    config->max_samples = samples;
    config->enable_integer_cfo_search = 0U;
    config->enable_profile_retry = 0U;
    config->enable_deep_receiver = 0U;
}

wrj_status_t wrj_module3_fast(const wrj_candidate_t *candidate,
    const wrj_cf32_t *iq, uint32_t count, const m3_config_t *config,
    m3_workspace_t *workspace, wrj_sync_result_t *result)
{
    wrj_status_t code;
    m3_config_t fast_config;
    if (candidate == NULL || iq == NULL || config == NULL || workspace == NULL ||
        result == NULL || result->input_baseband == NULL ||
        result->preprocessed_baseband == NULL || count == 0U ||
        count > workspace->max_samples) return WRJ_ERR_ARGUMENT;
    fast_config = *config;
    fast_config.enable_integer_cfo_search = 0U;
    fast_config.enable_profile_retry = 0U;
    fast_config.enable_deep_receiver = 0U;
    result->fs = candidate->sample_rate_hz;
    result->fc = candidate->center_frequency_hz;
    result->sample_count = count;
    result->sync_accepted = 0U;
    result->compensated_iq = NULL;
    /* Capture-domain preprocessing precedes centre shift, matching E34. */
    m3_cfo_compensate(iq, result->input_baseband, count,
        candidate->sample_rate_hz, candidate->candidate_center_offset_hz);
    memcpy(workspace->compensated, iq, sizeof(*iq) * (size_t)count);
    code = m3_preprocess_iq(workspace->compensated, count, workspace);
    if (code != WRJ_OK) return code;
    m3_cfo_compensate(workspace->compensated, result->preprocessed_baseband, count,
        candidate->sample_rate_hz, candidate->candidate_center_offset_hz);
    /* Production E34 FAST path; no legacy M4 observation adapters. */
    code = m3_run(candidate, iq, count, &fast_config, workspace, &result->sync);
    snprintf(result->sync.ble_origin_candidate,sizeof(result->sync.ble_origin_candidate),"%s",candidate->candidate_id);
    snprintf(result->sync.ble_origin_source,sizeof(result->sync.ble_origin_source),"%s",candidate->source_file);
    result->compensated_iq = workspace->compensated;
    result->sync_accepted = (uint8_t)(code == WRJ_OK &&
        result->sync.num_frames > 0U &&
        result->sync.sync_confidence >= fast_config.sync_accept_threshold);
    if (code == WRJ_OK) {
        snprintf(result->sync.status, sizeof(result->sync.status), "%s",
            result->sync.num_frames == 0U ? "sync_failed" :
            result->sync_accepted != 0U ? "ok" : "low_confidence");
    }
    return code;
}

wrj_status_t wrj_module3_deep(const wrj_candidate_t *candidate,
    const wrj_cf32_t *iq, uint32_t count, uint32_t attempt,
    m3_workspace_t *workspace, wrj_sync_result_t *result)
{
    if (candidate == NULL || iq == NULL || count == 0U || candidate->sample_rate_hz<=0.0f ||
        workspace == NULL || result == NULL || !result->input_baseband || !result->preprocessed_baseband ||
        count>workspace->max_samples || attempt < 1U || attempt > 2U)
        return WRJ_ERR_ARGUMENT;
    wrj_candidate_t selected=*candidate;const wrj_profile_kind_t original=result->sync.profile;
    if(strcmp(result->receiver_family,"DRONE")==0 && original!=WRJ_PROFILE_DRONEID_ZC){
        snprintf(selected.predicted_protocol_family,sizeof(selected.predicted_protocol_family),"DJI_DroneID");
        snprintf(selected.predicted_link_type,sizeof(selected.predicted_link_type),"DroneID");
    }else if(attempt==2U){
        if(original==WRJ_PROFILE_DRONEID_ZC){snprintf(selected.predicted_protocol_family,sizeof(selected.predicted_protocol_family),"Unknown");
            snprintf(selected.predicted_link_type,sizeof(selected.predicted_link_type),"Unknown");}
        else {snprintf(selected.predicted_protocol_family,sizeof(selected.predicted_protocol_family),"DJI_DroneID");
            snprintf(selected.predicted_link_type,sizeof(selected.predicted_link_type),"DroneID");}
    }
    m3_config_t cfg;wrj_module3_fast_config(&cfg,count);wrj_status_t code=wrj_module3_fast(&selected,iq,count,&cfg,workspace,result);
    if(code!=WRJ_OK)return code;
    /* Generic private CP cannot authorize an integer-bin change. Its DEEP
     * context therefore retains the received FAST correction. Drone public
     * pilot alias competition is handled by the bounded M4 specialist. */
    if(result->sync.profile!=WRJ_PROFILE_REMOTEID_BLE)return WRJ_OK;
    m3_result_t *best=calloc(1U,sizeof(*best)),*trial=calloc(1U,sizeof(*trial));
    wrj_ble_packet_evidence_t *packets=calloc(2048U,sizeof(*packets));
    if(!best || !trial || !packets){free(best);free(trial);free(packets);return WRJ_ERR_MEMORY;}
    *best=result->sync;
    int basic=0,location=0;
    for(uint32_t k=0U;k<best->ble_verified_packet_count;++k){basic|=best->ble_verified_pdu[k][14]>>4U==0U;location|=best->ble_verified_pdu[k][14]>>4U==1U;}
    if(basic && location){free(best);free(trial);free(packets);return WRJ_OK;}
    double anchors[2]={0.0,0.0};uint32_t anchor_count=1U;
    double delta=candidate->coarse_cfo_hz-result->sync.estimated_cfo_hz;
    if(isfinite(delta) && fabs(delta)<result->fs/2.0 && fabs(delta)>125e3)anchors[anchor_count++]=delta;
    static const double offsets[3]={0.0,-250e3,250e3},widths[2]={.9e6,.65e6};
    static const uint32_t out_sps[2]={16U,4U};double best_score=best->ble_verified_packet_count;
    for(uint32_t anchor=0U;anchor<anchor_count;++anchor){uint32_t packet_count=0U;
        for(uint32_t offset=0U;offset<3U;++offset)for(uint32_t width=0U;width<2U;++width)for(uint32_t rate=0U;rate<2U;++rate){
            memset(trial,0,sizeof(*trial));
            snprintf(trial->ble_origin_candidate,sizeof(trial->ble_origin_candidate),"%s",candidate->candidate_id);
            snprintf(trial->ble_origin_source,sizeof(trial->ble_origin_source),"%s",candidate->source_file);
            uint32_t extra=0U;
            code=wrj_ble_filter_decode_e34_ex(result->compensated_iq,count,result->fs,anchors[anchor]+offsets[offset],
                widths[width],out_sps[rate],trial,packets+packet_count,2048U-packet_count,&extra,
                offset==0U && width==0U && rate==0U);
            if(code!=WRJ_OK)goto finish;packet_count+=extra;
        }
        wrj_ble_gap_recover_e34(packets,packet_count,(uint32_t)lround(result->fs/1e6),trial);
        basic=location=0;int conflict=0;const uint8_t *identity=NULL;double correlation=0.0;
        for(uint32_t k=0U;k<trial->ble_verified_packet_count;++k){const uint8_t *pdu=trial->ble_verified_pdu[k];
            correlation+=trial->ble_verified_confidence[k];
            if(pdu[14]>>4U==0U){basic=1;if(identity && memcmp(identity,pdu+16U,20U)!=0)conflict=1;identity=pdu+16U;}
            location|=pdu[14]>>4U==1U;
        }
        if(conflict)continue;
        double confidence=.55*correlation/WRJ_MAX(1U,trial->ble_verified_packet_count)+
            .25*WRJ_MIN(1.0,trial->ble_verified_packet_count/2.0)+.20*(basic && location);
        double score=100.0*(basic && location)+trial->ble_verified_packet_count+confidence;
        if(score>best_score){best_score=score;
            best->ble_verified_packet_count=trial->ble_verified_packet_count;
            memcpy(best->ble_verified_pdu,trial->ble_verified_pdu,sizeof(best->ble_verified_pdu));
            memcpy(best->ble_verified_raw_bits,trial->ble_verified_raw_bits,sizeof(best->ble_verified_raw_bits));
            memcpy(best->ble_verified_crc,trial->ble_verified_crc,sizeof(best->ble_verified_crc));
            memcpy(best->ble_verified_start,trial->ble_verified_start,sizeof(best->ble_verified_start));
            memcpy(best->ble_verified_confidence,trial->ble_verified_confidence,sizeof(best->ble_verified_confidence));
            best->ble_sync_confidence=(float)confidence;
        }
    }
    result->sync=*best;
finish:
    free(best);free(trial);free(packets);return code;
}
