function export_matlab_cases(projectRoot)
%EXPORT_MATLAB_CASES Export four Module1/2 candidate IQ artifacts to CF32.
% This is a host-side data conversion utility only. It never changes the
% MATLAB algorithms, handoff CSV, or formal result directory.

if nargin < 1 || isempty(projectRoot)
    projectRoot=fileparts(fileparts(mfilename('fullpath')));
end
resultRoot=fullfile(projectRoot,'results_wrj_m12_unknown_58g_final');
artifactRoot=fullfile(resultRoot,'05_DATA','module3_candidate_iq');
dataRoot=fullfile(fileparts(fileparts(mfilename('fullpath'))),'data','cases');
if ~isfolder(dataRoot); mkdir(dataRoot); end

ids={ ...
    'sim_dji_wideband_018_M001'; ...
    'sim_autel_wideband_001_M001'; ...
    'sim_autel_wideband_012_M001'; ...
    'sim_autel_wideband_015_M001'; ...
    'sim_dji_droneid_013_M001'; ...
    'sim_dji_droneid_022_M001'; ...
    'sim_remoteid_ble_005_M001'; ...
    'sim_remoteid_ble_028_M001'; ...
    'sim_dji_control_004_M001'; ...
    'sim_autel_control_009_M005'; ...
    'sim_unknown_uav_005_M003'};

for k=1:numel(ids)
    id=ids{k};
    source=fullfile(artifactRoot,[id '.mat']);
    if ~isfile(source)
        error('WRJ_ARM_Port:MissingArtifact','Missing candidate IQ artifact: %s',source);
    end
    S=load(source,'iq','fs','candidateId','sourceFile');
    if ~isfield(S,'iq') || ~isa(S.iq,'single')
        error('WRJ_ARM_Port:InvalidArtifact','Artifact %s has no single-precision iq vector.',source);
    end
    target=fullfile(dataRoot,[id '.cf32']);
    fid=fopen(target,'w','ieee-le');
    if fid<0; error('WRJ_ARM_Port:WriteFailed','Cannot write %s.',target); end
    cleanup=onCleanup(@() fclose(fid)); %#ok<NASGU>
    interleaved=zeros(2*numel(S.iq),1,'single');
    interleaved(1:2:end)=real(S.iq(:));
    interleaved(2:2:end)=imag(S.iq(:));
    fwrite(fid,interleaved,'single');
    clear cleanup
    fprintf('exported %s: %d complex float32 samples at %.0f Hz%s',id,numel(S.iq),S.fs,newline);
end
end
