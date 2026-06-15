# SUEPTron

Processes MINIAOD for Run 3 2024 and 2025 to Scouting NANO - with further branches for SUEP analyses. 

Initialize environment and GRID
```
source /cvmfs/cms.cern.ch/cmsset_default.sh
source /cvmfs/grid.cern.ch/alma9-ui-current/etc/profile.d/setup-alma9-test.sh
voms-proxy-init --rfc -voms cms --valid 192:00
```
Intialize CMSSW:
```
scram list CMSSW

cmsrel CMSSW_16_0_3
cd CMSSW_16_0_3/src

cmsenv
```

Clone Repo and build:
```
git clone https://github.com/SulaymanA/SUEPTron.git
scram b clean
scram b
```

This repo is designed for producing Scouting NanoAOD ntuples and currently only supports 2024/2025 data or MC. There are various option to customise the ntuples such as including rechits (2025 data only), saving PFCandidates or saving only chraged tracks. Below are examples of compatible files:

```
cmsRun SUEPTron/HLTSCOUTNANOProducer/python/suepton_cfg.py inputFile=/store/data/Run2024D/ScoutingPFMonitor/MINIAOD/PromptReco-v1/000/380/306/00000/f2b70a80-5047-4b29-8d5b-18f83d927320.root outputFile=out.root isData=True year=2024 numEvents=10

cmsRun SUEPTron/HLTSCOUTNANOProducer/python/suepton_cfg.py inputFile=/store/data/Run2024D/ScoutingPFMonitor/MINIAOD/PromptReco-v1/000/380/306/00000/f2b70a80-5047-4b29-8d5b-18f83d927320.root outputFile=out.root isData=True year=2024 savePFCands=True saveTracks=True numEvents=10
```