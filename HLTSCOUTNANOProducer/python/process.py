import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from Configuration.Eras.Era_Run3_2024_cff import Run3_2024
from Configuration.Eras.Era_Run3_2025_cff import Run3_2025

params = VarParsing()
params.register('inputFile',        'file:',         VarParsing.multiplicity.singleton, VarParsing.varType.string, 'Input file')
params.register('outputFile',       'suepton.root',  VarParsing.multiplicity.singleton, VarParsing.varType.string, 'Output NanoAOD file')
params.register('numEvents',        -1,              VarParsing.multiplicity.singleton, VarParsing.varType.int,    'Number of events (-1 = all)')
params.register('saveTracks',  False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include Track table')
params.register('saveRechits', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include Rechits (2025E onwards)')
params.register('savePFCands', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include PFCandidates (2025E onwards)')
params.register('saveJetsRecluster', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include PFCandidates (2025E onwards)')
params.register('saveMuonVtx', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include PFCandidates (2025E onwards)')
params.register('saveJets', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include PFCandidates (2025E onwards)')
params.register('saveTriggerTable', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include TriggerTable (denominator + Ht_corrected)')
params.register('saveMLTable',      False, VarParsing.multiplicity.singleton, VarParsing.varType.bool,   'Include MLTable (ABCDDiScOTEC var1/var2)')
params.register('mlModel',          '',    VarParsing.multiplicity.singleton, VarParsing.varType.string, 'Path to ABCDDiScOTEC ONNX model file')
params.register('jecFile',    '',   VarParsing.multiplicity.singleton, VarParsing.varType.string, 'Path to JEC JSON file (required when saveTriggerTable=True and applyJEC=True)')
params.register('jecTag',     'Winter24HLT_V1_MC', VarParsing.multiplicity.singleton, VarParsing.varType.string, 'JEC tag inside the JEC JSON file')
params.register('applyJEC',   False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Apply HLT JEC corrections in TriggerTable producer')
params.register('year',       2024, VarParsing.multiplicity.singleton, VarParsing.varType.int,    'Year')
params.register('isData',     True, VarParsing.multiplicity.singleton, VarParsing.varType.bool,   'Specify whether MC or data.')
params.register('fromMini',   True, VarParsing.multiplicity.singleton, VarParsing.varType.bool,   'Input is ScoutingPFMonitor/MiniAOD (True) or ScoutingPFRun3 (False)')
params.register('goldenJSON',  '',  VarParsing.multiplicity.singleton, VarParsing.varType.string, 'Path to Golden JSON for lumi masking (data only)')

params.parseArguments()


if params.year == 2024:
    era = Run3_2024
elif params.year == 2025:
    era = Run3_2025

process = cms.Process('SUEPTron', era)

process.load("FWCore.MessageService.MessageLogger_cfi")
process.MessageLogger.cerr.FwkReport.reportEvery = 1

process.options = cms.untracked.PSet(
    wantSummary    = cms.untracked.bool(True),
    TryToContinue  = cms.untracked.vstring('ProductNotFound'),
)
process.maxEvents = cms.untracked.PSet(input=cms.untracked.int32(params.numEvents))

process.source = cms.Source("PoolSource",
    fileNames=cms.untracked.vstring(params.inputFile),
)

if params.isData and params.goldenJSON:
    import FWCore.PythonUtilities.LumiList as LumiList
    process.source.lumisToProcess = LumiList.LumiList(filename=params.goldenJSON).getVLuminosityBlockRange()

# ── Infrastructure ────────────────────────────────────────────────────────────
# ONNX runtime service (needed by ParticleNet jet taggers)
process.load('Configuration.StandardSequences.Services_cff')
# Geometry + B-field (needed by jet reclustering producers)
process.load('Configuration.StandardSequences.GeometryRecoDB_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('PhysicsTools.NanoAOD.nanogen_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
from Configuration.AlCa.GlobalTag import GlobalTag

# For 2024 data use e.g. '140X_dataRun3_HLT_v3'; this default is for 2025 MC
if params.isData:
    if params.year == 2024:
        process.GlobalTag = GlobalTag(process.GlobalTag, '140X_dataRun3_v14', '')
    elif params.year == 2025:
        process.GlobalTag = GlobalTag(process.GlobalTag, '140X_dataRun3_v14', '')
else:
    if params.year == 2024:
        process.GlobalTag = GlobalTag(process.GlobalTag, '140X_mcRun3_2024_realistic_v10', '')
    elif params.year == 2025:
        process.GlobalTag = GlobalTag(process.GlobalTag, '142X_mcRun3_2025_realistic_v7', '')

# ── Standard CMS Run3 scouting NanoAOD sequences ──────────────────────────────
# Era Run3_2024 activates run3_scouting_2024: ScoutingMuonVtx/NoVtx, electron best-track
process.load('PhysicsTools.NanoAOD.custom_run3scouting_cff')

# ── MC-only: gen-level sequence and gen-jet matching for reclustered jets ──────
if not params.isData:
    process.load('SimGeneral.MixingModule.mixNoPU_cfi')
    from PhysicsTools.NanoAOD.nanogen_cff import customizeNanoGENFromMini
    process = customizeNanoGENFromMini(process)
    from PhysicsTools.NanoAOD.custom_run3scouting_cff import scoutingNanoTaskMC
    process.scoutingNanoSequence.associate(scoutingNanoTaskMC)
    from PhysicsTools.NanoAOD.globals_cff import puTable
    process.puTable = puTable

# ── RecHit producer ───────────────────────────────────────────────────────────
process.recHitProducer = cms.EDProducer('RecHitProducer',
    RecHitEE   = cms.InputTag("hltScoutingRecHitPacker", "EE",   "HLT"),
    RecHitEB   = cms.InputTag("hltScoutingRecHitPacker", "EB",   "HLT"),
    RecHitHBHE = cms.InputTag("hltScoutingRecHitPacker", "HBHE", "HLT"),
)

# ── SUEP producer ─────────────────────────────────────────────────────────────
process.SUEPTable = cms.EDProducer('SUEPProducer',
    ScoutingParticle = cms.InputTag("hltScoutingPFPacker"),
    ScoutingElectron = cms.InputTag("hltScoutingEgammaPacker"),
    ScoutingMuon     = cms.InputTag("hltScoutingMuonPackerVtx"),
    saveTracks = cms.bool(params.saveTracks),
)

# ── Trigger efficiency producer ────────────────────────────────────────────────
# Produces TriggerTable: denominator (SingleMuon DST + tight muon) and
# Ht_corrected (JEC-corrected jet HT after jetID + muon cleaning).
# Enable with saveTriggerTable=True; set applyJEC=True + jecFile=<path> for JECs.
process.TriggerTable = cms.EDProducer('TriggerEfficiencyProducer',
    JetTag         = cms.InputTag("hltScoutingPFPacker"),
    MuonTag        = cms.InputTag("hltScoutingMuonPackerVtx"),
    RhoTag         = cms.InputTag("hltScoutingPFPacker", "rho"),
    TriggerResults = cms.InputTag("TriggerResults", "", "HLT"),
    SingleMuonPath = cms.string("DST_PFScouting_SingleMuon"),
    applyJEC       = cms.bool(params.applyJEC),
    JECFile        = cms.string(params.jecFile),
    JECTag         = cms.string(params.jecTag),
)

# ── ML inference producer (ABCDDiScOTEC) ──────────────────────────────────────
# Produces MLTable: var1, var2 from the GNN model.
# Enable with saveMLTable=True; provide mlModel=<path_to_onnx_file>.
process.MLTable = cms.EDProducer('MLProducer',
    ParticleTag   = cms.InputTag("hltScoutingPFPacker"),
    ModelPath     = cms.string(params.mlModel),
    k             = cms.int32(10),
    nParticlesMax = cms.int32(1000),
)

# ── NanoAOD output ────────────────────────────────────────────────────────────
process.NANOAODoutput = cms.OutputModule("NanoAODOutputModule",
    fileName             = cms.untracked.string(params.outputFile),
    compressionLevel     = cms.untracked.int32(9),
    compressionAlgorithm = cms.untracked.string("LZMA"),
    outputCommands       = cms.untracked.vstring(
        ["drop *",
         "keep nanoaodFlatTable_SUEPTable_*_*",
         "keep edmTriggerResults_*_*_*",
        ]
        + (["keep nanoaodFlatTable_puTable_*_*",
             "keep nanoaodFlatTable_genWeightsTable_*_*"] if not params.isData else [])
        + (["keep nanoaodFlatTable_TriggerTable_*_*"] if params.saveTriggerTable else [])
        + (["keep nanoaodFlatTable_MLTable_*_*"]       if params.saveMLTable      else [])
        + (["keep *_scoutingPFCandidateTable_*_*"] if params.savePFCands else [])
        + (["keep *_scoutingMuonVtxTable_*_*"] if params.saveMuonVtx else [])
        + (["keep *_scoutingPFJetTable_*_*", "keep *_scoutingRhoTable_*_*"] if params.saveJets else [])
        + (["keep *_scoutingPFJetReclusterTable_*_*", "keep *_scoutingRhoTable_*_*"] if params.saveJetsRecluster else [])
    ),
)

# ── Paths and schedule ────────────────────────────────────────────────────────
_suepPath = process.scoutingNanoSequence + process.SUEPTable
if not params.isData:
    _suepPath += process.puTable
if params.saveTriggerTable:
    _suepPath += process.TriggerTable
if params.saveMLTable:
    _suepPath += process.MLTable
if params.saveRechits:
    _suepPath = process.scoutingNanoSequence + process.recHitProducer + process.SUEPTable
    if params.saveTriggerTable:
        _suepPath += process.TriggerTable
    if params.saveMLTable:
        _suepPath += process.MLTable
process.p = cms.Path(_suepPath)

process.endk = cms.EndPath(process.NANOAODoutput)

# schedule must be defined before calling customisers that extend it
if not params.isData:
    process.nanoAOD_gen_step = cms.Path(process.nanogenSequence)
    process.schedule = cms.Schedule(process.nanoAOD_gen_step, process.p, process.endk)
else:
    process.schedule = cms.Schedule(process.p, process.endk)

# ── Scouting customisations ───────────────────────────────────────────────────
from PhysicsTools.NanoAOD.custom_run3scouting_cff import addScoutingPFCandidate, customiseScoutingNano, customiseScoutingNanoFromMini
process = addScoutingPFCandidate(process)
if params.fromMini:
    # ScoutingPFMonitor/MiniAOD: gtStage2Digis exists → L1 trigger tables available
    process = customiseScoutingNano(process)
    process = customiseScoutingNanoFromMini(process)
# ScoutingPFRun3: no gtStage2Digis or hltFEDSelectorL1 in MINIAOD → skip L1 trigger tables
# HLT decisions are still saved via "keep edmTriggerResults_*_*_*"
