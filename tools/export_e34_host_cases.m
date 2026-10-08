function export_e34_host_cases(root)
% Acquisition files are separate from comparator-only Golden artifacts.
    if nargin<1;root=fileparts(fileparts(fileparts(mfilename('fullpath'))));end
    port=fileparts(fileparts(mfilename('fullpath')));out=fullfile(port,'results_c_validation','host53');
    inputs=fullfile(out,'inputs');gold=fullfile(out,'golden_comparator_only');
    if ~isfolder(inputs);mkdir(inputs);end;if ~isfolder(gold);mkdir(gold);end
    reference=fullfile(root,'results');
    handoff=readtable(fullfile(root,'results','02_MODULE2_CLASSIFICATION','module12_to_module3_handoff.csv'),'TextType','string');
    summary=readtable(fullfile(reference,'06_MODULE3_FREQUENCY_FRAME_SYNC','01_TABLES','module3_sync_summary.csv'),'TextType','string');
    columns={'candidateId','sourceFile','predictedLinkType','predictedProtocolFamily','candidateStartSec', ...
        'candidateEndSec','sampleRateHz','centerFrequencyHz','candidateCenterOffsetHz','bandwidthHz', ...
        'coarseCfoHz','framePeriodEstimateSec','frameStructureScore','preambleRepeatScore','syncStrategy', ...
        'parserTemplateId','candidateIqArtifact','recommendedGuardSec'};
    receiver=handoff([],columns);m3=struct([]);frames=struct([]);pdu=struct([]);
    for k=1:height(summary)
        id=summary.candidateId(k);h=handoff(handoff.candidateId==id,:);assert(height(h)==1);
        actual=load(fullfile(root,h.candidateIqArtifact),'iq','fs','fc','candidateCenterOffsetHz','candidateBandwidthHz');
        write_cf32(fullfile(inputs,id+".cf32"),actual.iq);
        row=h(:,columns);row.candidateIqArtifact=id+".cf32";receiver=[receiver;row]; %#ok<AGROW>
        artifact=load(summary.artifactFile(k),'compensatedSegment','frameStarts','frameConfidences','syncProfile','remoteIdInfo');
        write_cf32(fullfile(gold,id+".compensated.cf32"),artifact.compensatedSegment);
        entry=struct('candidate',char(id),'status',char(summary.status(k)), ...
            'syncAccepted',summary.syncAccepted(k),'profile',artifact.syncProfile.name, ...
            'CFOHz',summary.estimatedCFOHz(k),'SfoPpm',summary.estimatedSfoPpm(k));
        if isempty(m3);m3=entry;else;m3(end+1)=entry;end %#ok<AGROW>
        for j=1:numel(artifact.frameStarts)
            row=struct('candidate',char(id),'index',j,'start0',double(artifact.frameStarts(j))-1,'confidence',artifact.frameConfidences(j));
            if isempty(frames);frames=row;else;frames(end+1)=row;end %#ok<AGROW>
        end
        packets=artifact.remoteIdInfo.packetDiagnostics;
        if ~isempty(packets)
            path=fullfile(gold,id+".m3_packets.json");fid=fopen(path,'w');fprintf(fid,'%s',jsonencode(packets));fclose(fid);
            if isempty(pdu);disp('BLE_PACKET_SCHEMA');disp(fieldnames(packets));pdu=packets(1);end
        end
    end
    writetable(receiver,fullfile(inputs,'handoff.csv'));writetable(struct2table(m3),fullfile(gold,'m3_summary.csv'));
    writetable(struct2table(frames),fullfile(gold,'m3_frames.csv'));
    F=load(fullfile(reference,'09_FINAL_RECEIVER_EVIDENCE','receiver_evidence.mat'),'final');
    disp('FINAL_RECORD_SCHEMA');disp(fieldnames(F.final.records));disp(size(F.final.records));
    fid=fopen(fullfile(gold,'final_records.json'),'w');fprintf(fid,'%s',jsonencode(F.final.records));fclose(fid);
    for name={'receiver_parse_results.csv','receiver_parsed_fields.csv','receiver_remoteid_metrics.csv'}
        copyfile(fullfile(reference,'09_FINAL_RECEIVER_EVIDENCE',name{1}),fullfile(gold,name{1}));
    end
    copyfile(fullfile(reference,'merged_end_to_end_cases.csv'),fullfile(gold,'merged_end_to_end_cases.csv'));
    copyfile(fullfile(reference,'merged_frame_recovery.csv'),fullfile(gold,'merged_frame_recovery.csv'));
    copyfile(fullfile(reference,'05_DATA','protocol_frame_truth.csv'),fullfile(gold,'protocol_frame_truth.csv'));
    copyfile(fullfile(reference,'08_MODULE5_STATE_MACHINE_PARSER','01_TABLES','module5_parse_results.csv'), ...
        fullfile(gold,'module5_parse_results.csv'));
    first=load(fullfile(reference,'module4_result.mat'));disp('FIRST_RESULT_SCHEMA');disp(fieldnames(first));
    fid=fopen(fullfile(gold,'fast_records.json'),'w');assert(fid>=0);
    fprintf(fid,'%s',jsonencode(first.module4Result.legacyResult.records));fclose(fid);
    for key=fieldnames(first).'
        value=first.(key{1});if isstruct(value);disp(fieldnames(value));end
    end
    fprintf('HOST53_EXPORT acquisition=%d M1_M2_runs=0\n',height(receiver));
end
function write_cf32(path,x)
    fid=fopen(path,'w','ieee-le');assert(fid>=0);cleanup=onCleanup(@()fclose(fid)); %#ok<NASGU>
    data=zeros(2*numel(x),1,'single');data(1:2:end)=real(x(:));data(2:2:end)=imag(x(:));
    assert(fwrite(fid,data,'single')==numel(data));
end
