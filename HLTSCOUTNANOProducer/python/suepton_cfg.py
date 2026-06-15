import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing
from Configuration.Eras.Era_Run3_2024_cff import Run3_2024

params = VarParsing()
params.register('inputFile',        'file:',         VarParsing.multiplicity.singleton, VarParsing.varType.string, 'Input file')
params.register('outputFile',       'suepton.root',  VarParsing.multiplicity.singleton, VarParsing.varType.string, 'Output NanoAOD file')
params.register('numEvents',        -1,              VarParsing.multiplicity.singleton, VarParsing.varType.int,    'Number of events (-1 = all)')
params.register('saveTracks',  False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include Track table')
params.register('saveRechits', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include Rechits (2025E onwards)')
params.register('savePFCands', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include PFCandidates (2025E onwards)')
params.register('saveJets', False, VarParsing.multiplicity.singleton, VarParsing.varType.bool, 'Include PFCandidates (2025E onwards)')
params.parseArguments()

process = cms.Process('SUEPTron', Run3_2024)

process.load("FWCore.MessageService.MessageLogger_cfi")
process.MessageLogger.cerr.FwkReport.reportEvery = 100

process.options = cms.untracked.PSet(wantSummary=cms.untracked.bool(True))
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

process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')
from Configuration.AlCa.GlobalTag import GlobalTag
# For 2024 data use e.g. '140X_dataRun3_HLT_v3'; this default is for 2025 MC
process.GlobalTag = GlobalTag(process.GlobalTag, '142X_mcRun3_2025_realistic_v7', '')

# ── Standard CMS Run3 scouting NanoAOD sequences ──────────────────────────────
# Provides scoutingNanoSequence: ScoutingMuon, ScoutingElectron, ScoutingPhoton,
# ScoutingPrimaryVertex, ScoutingPFJet, ScoutingMET, ScoutingRho,
# ScoutingPFJetRecluster (AK4+ParticleNet), ScoutingFatPFJetRecluster (AK8+ParticleNet+GlobalParT)
# Era Run3_2024 automatically activates run3_scouting_2024 modifier:
#   - uses ScoutingMuonVtx + ScoutingMuonNoVtx instead of ScoutingMuon
#   - uses best-track selection for ScoutingElectron
process.load('PhysicsTools.NanoAOD.custom_run3scouting_cff')

# ── RecHit producer ───────────────────────────────────────────────────────────
process.recHitProducer = cms.EDProducer('RecHitProducer',
    RecHitEE   = cms.InputTag("hltScoutingRecHitPacker", "EE",   "HLT"),
    RecHitEB   = cms.InputTag("hltScoutingRecHitPacker", "EB",   "HLT"),
    RecHitHBHE = cms.InputTag("hltScoutingRecHitPacker", "HBHE", "HLT"),
)

# ── SUEP producer ─────────────────────────────────────────────────────────────
process.suepProducer = cms.EDProducer('SUEPProducer',
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

# ── Paths ─────────────────────────────────────────────────────────────────────
# scoutingNanoSequence runs all standard scouting tables
# suepProducer adds SuepEvent (+ optional Track/ScoutingPFCandidate)
process.p = cms.Path(process.scoutingNanoSequence + process.suepProducer)
if params.saveRechits:
    process.p = cms.Path(process.scoutingNanoSequence + process.recHitProducer + process.suepProducer)

process.endk = cms.EndPath(process.NANOAODoutput)

# Automatic addition of the customisation function from PhysicsTools.NanoAOD.custom_run3scouting_cff
from PhysicsTools.NanoAOD.custom_run3scouting_cff import addScoutingPFCandidate,customiseScoutingNano 

#call to customisation function addScoutingPFCandidate imported from PhysicsTools.NanoAOD.custom_run3scouting_cff
process = addScoutingPFCandidate(process)
