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
params.register('saveJets', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include PFCandidates (2025E onwards)')
params.register('year', 2024, VarParsing.multiplicity.singleton, VarParsing.varType.int, 'Year')
params.register('isData', True, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Specify whether MC or data.')

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
    fileNames=cms.untracked.vstring(params.inputFile)
)

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
        + (["keep *_scoutingPFCandidateTable_*_*"] if params.savePFCands else [])
        + (["keep *_scoutingPFJetReclusterTable_*_*", "keep *_scoutingRhoTable_*_*"] if params.saveJets else [])
    ),
)

# ── Paths and schedule ────────────────────────────────────────────────────────
process.p = cms.Path(process.scoutingNanoSequence + process.SUEPTable)
if params.saveRechits:
    process.p = cms.Path(process.scoutingNanoSequence + process.recHitProducer + process.SUEPTable)

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
process = customiseScoutingNano(process)        # adds L1 trigger step; on MC adds gen-jet matching tasks
process = customiseScoutingNanoFromMini(process) # replaces gtStage2DigisScouting for MiniAOD/PFMonitor inputs
