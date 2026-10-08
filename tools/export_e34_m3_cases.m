function export_e34_m3_cases(matlab_root)
% Export acquisition IQ only; Golden receiver artifacts are comparator-only.
    if nargin < 1
        matlab_root = fileparts(fileparts(fileparts(mfilename('fullpath'))));
    end
    port_root = fileparts(fileparts(mfilename('fullpath')));
    output_root = fullfile(port_root,'results_c_validation','m3_core');
    input_root = fullfile(output_root,'inputs');
    golden_root = fullfile(output_root,'golden_comparator_only');
    if ~isfolder(input_root); mkdir(input_root); end
    if ~isfolder(golden_root); mkdir(golden_root); end
    handoff = readtable(fullfile(matlab_root,'results','02_MODULE2_CLASSIFICATION', ...
        'module12_to_module3_handoff.csv'),'TextType','string','VariableNamingRule','preserve');
    summary = readtable(fullfile(matlab_root,'results', ...
        '06_MODULE3_FREQUENCY_FRAME_SYNC','01_TABLES','module3_sync_summary.csv'), ...
        'TextType','string','VariableNamingRule','preserve');
    ids = ["sim_dji_control_017_M001"; "sim_dji_wideband_018_M001"; ...
        "sim_dji_wideband_027_M001"; "sim_autel_control_010_M001"; ...
        "sim_autel_wideband_012_M001"; "sim_autel_wideband_017_M001"; ...
        "sim_dji_droneid_013_M001"; "sim_remoteid_ble_005_M001"; ...
        "sim_remoteid_ble_028_M001"; "sim_unknown_uav_005_M003"; ...
        "sim_autel_wideband_001_M001"; "sim_dji_droneid_010_M001"];
    api = Module3(struct('operation','receiver_api','receiverMode','FAST'));
    input_columns = {'candidateId','sourceFile','predictedLinkType', ...
        'predictedProtocolFamily','candidateStartSec','candidateEndSec', ...
        'sampleRateHz','centerFrequencyHz','candidateCenterOffsetHz','bandwidthHz', ...
        'coarseCfoHz','framePeriodEstimateSec','frameStructureScore','preambleRepeatScore', ...
        'syncStrategy','parserTemplateId','candidateIqArtifact','recommendedGuardSec'};
    receiver_handoff = handoff([] ,input_columns);
    golden_rows = struct([]);
    frame_rows = struct('candidate',{},'index',{},'start0',{},'confidence',{});
    for index = 1:numel(ids)
        h = handoff(handoff.candidateId==ids(index),:);
        s = summary(summary.candidateId==ids(index),:);
        assert(height(h)==1 && height(s)==1,'Unique candidate required');
        source_path = fullfile(matlab_root,h.candidateIqArtifact);
        % Never load evaluationTruth/evaluationFrameTruth/evaluationFieldTruth.
        input = load(source_path,'iq','fs','fc','candidateCenterOffsetHz','candidateBandwidthHz');
        golden = load(s.artifactFile,'inputBasebandSegment','preprocessedInputBasebandSegment', ...
            'compensatedSegment','frameStarts','frameConfidences','syncProfile','primaryCFOHz','estimatedSfoPpm');
        assert(isa(input.iq,'single') && numel(input.iq)==numel(golden.compensatedSegment));
        input_file = fullfile(input_root,ids(index)+".cf32");
        write_cf32(input_file,input.iq);
        [preprocessed,~] = api.preprocess(input.iq);
        write_cf32(fullfile(golden_root,ids(index)+".preprocessed_capture.cf32"),preprocessed);
        raw = double(input.iq(:)); raw(~isfinite(raw)) = 0;
        axis_seconds = (0:numel(raw)-1)'/input.fs;
        raw_baseband = single(raw.*exp(-1j*2*pi*input.candidateCenterOffsetHz*axis_seconds));
        assert(isequal(raw_baseband,golden.inputBasebandSegment),'Raw IQ coordinate mismatch');
        write_cf32(fullfile(golden_root,ids(index)+".raw_baseband.cf32"),golden.inputBasebandSegment);
        write_cf32(fullfile(golden_root,ids(index)+".preprocessed_baseband.cf32"),golden.preprocessedInputBasebandSegment);
        write_cf32(fullfile(golden_root,ids(index)+".compensated.cf32"),golden.compensatedSegment);
        row = h(:,input_columns);
        row.candidateIqArtifact = ids(index)+".cf32";
        receiver_handoff = [receiver_handoff;row]; %#ok<AGROW>
        golden_rows(index).candidate = char(ids(index)); %#ok<AGROW>
        golden_rows(index).status = char(s.status);
        golden_rows(index).syncAccepted = double(s.syncAccepted);
        golden_rows(index).profile = golden.syncProfile.name;
        golden_rows(index).CFOHz = double(s.estimatedCFOHz);
        golden_rows(index).SfoPpm = double(s.estimatedSfoPpm);
        golden_rows(index).frameCount = numel(golden.frameStarts);
        golden_rows(index).frameLength = double(s.estimatedFrameLengthSamples);
        golden_rows(index).sourceArtifact = char(source_path);
        golden_rows(index).goldenArtifact = char(s.artifactFile);
        for k = 1:numel(golden.frameStarts)
            frame_rows(end+1) = struct('candidate',char(ids(index)),'index',k, ...
                'start0',double(golden.frameStarts(k))-1,'confidence',double(golden.frameConfidences(k))); %#ok<AGROW>
        end
        fprintf('EXPORTED %s samples=%d Golden=COMPARATOR_ONLY\n',ids(index),numel(input.iq));
    end
    writetable(receiver_handoff,fullfile(input_root,'handoff.csv'));
    writetable(struct2table(golden_rows),fullfile(golden_root,'summary.csv'));
    writetable(struct2table(frame_rows),fullfile(golden_root,'frames.csv'));
end

function write_cf32(path,value)
    handle = fopen(path,'w','ieee-le');
    assert(handle>=0,'Cannot create output');
    cleanup = onCleanup(@() fclose(handle)); %#ok<NASGU>
    buffer = zeros(2*numel(value),1,'single');
    buffer(1:2:end) = real(value(:)); buffer(2:2:end) = imag(value(:));
    assert(fwrite(handle,buffer,'single')==numel(buffer));
end
