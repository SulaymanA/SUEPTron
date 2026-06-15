#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "DataFormats/Scouting/interface/Run3ScoutingParticle.h"
#include "DataFormats/Scouting/interface/Run3ScoutingElectron.h"
#include "DataFormats/Scouting/interface/Run3ScoutingMuon.h"

#include "SUEPTron/HLTSCOUTNANOProducer/interface/SuepEvent.h"
#include "fastjet/ClusterSequence.hh"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

class SUEPProducer : public edm::global::EDProducer<> {
public:
    explicit SUEPProducer(const edm::ParameterSet&);
    ~SUEPProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions&);
    void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
    const edm::EDGetTokenT<std::vector<Run3ScoutingParticle>>  particleToken_;
    const edm::EDGetTokenT<std::vector<Run3ScoutingElectron>>  electronToken_;
    const edm::EDGetTokenT<std::vector<Run3ScoutingMuon>>      muonToken_;

    const bool saveTracks_;
};

SUEPProducer::SUEPProducer(const edm::ParameterSet& iConfig)
    : particleToken_  (consumes(iConfig.getParameter<edm::InputTag>("ScoutingParticle"))),
      electronToken_  (consumes(iConfig.getParameter<edm::InputTag>("ScoutingElectron"))),
      muonToken_      (consumes(iConfig.getParameter<edm::InputTag>("ScoutingMuon"))),
      saveTracks_     (iConfig.getParameter<bool>("saveTracks"))
{
    produces<nanoaod::FlatTable>("SuepEvent");
    if (saveTracks_)
        produces<nanoaod::FlatTable>("Track");
}

void SUEPProducer::produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup&) const {

    edm::Handle<std::vector<Run3ScoutingParticle>>  particles;
    edm::Handle<std::vector<Run3ScoutingElectron>>  electrons;
    edm::Handle<std::vector<Run3ScoutingMuon>>      muons;
    iEvent.getByToken(particleToken_, particles);
    iEvent.getByToken(electronToken_, electrons);
    iEvent.getByToken(muonToken_,     muons);

    // ── SUEP track selection ──────────────────────────────────────────────────
    std::vector<fastjet::PseudoJet> suepInputs;
    std::vector<float> trk_pt, trk_eta;
    for (auto const& pf : *particles) {
        int absPdg = std::abs(pf.pdgId());
        if (pf.pt() > 0.75f &&
            std::abs(pf.eta()) < 2.4f &&
            pf.vertex() == 0 &&
            (absPdg == 11 || absPdg == 13 || absPdg == 211)) {
            float pz = pf.pt() * std::sinh(pf.eta());
            float px = pf.pt() * std::cos(pf.phi());
            float py = pf.pt() * std::sin(pf.phi());
            float e  = pf.pt() * std::cosh(pf.eta());
            suepInputs.emplace_back(px, py, pz, e);
            trk_pt.push_back(pf.pt());
            trk_eta.push_back(pf.eta());
        }
    }

    // ── AK4 HT from full PF event ─────────────────────────────────────────────
    std::vector<fastjet::PseudoJet> allPF;
    allPF.reserve(particles->size());
    for (auto const& pf : *particles) {
        float pz = pf.pt() * std::sinh(pf.eta());
        float px = pf.pt() * std::cos(pf.phi());
        float py = pf.pt() * std::sin(pf.phi());
        float e  = pf.pt() * std::cosh(pf.eta());
        allPF.emplace_back(px, py, pz, e);
    }
    float ht = 0.f;
    if (!allPF.empty()) {
        fastjet::ClusterSequence csAK4(allPF, fastjet::JetDefinition(fastjet::antikt_algorithm, 0.4));
        for (auto const& jet : fastjet::sorted_by_pt(csAK4.inclusive_jets())) {
            if (jet.pt() > 30. && std::abs(jet.eta()) < 2.5)
                ht += static_cast<float>(jet.pt());
        }
    }

    float maxElePt = 0.f, maxMuPt = 0.f;
    for (auto const& e : *electrons) maxElePt = std::max(maxElePt, e.pt());
    for (auto const& m : *muons)     maxMuPt  = std::max(maxMuPt,  m.pt());

    // ── SUEP sphericity ───────────────────────────────────────────────────────
    SuepEvent suep(suepInputs);

    auto suepTable = std::make_unique<nanoaod::FlatTable>(1, "SuepEvent", true);
    suepTable->addColumnValue<float>("sphericitySUEP",    suep.getSphericitySUEP(),    "Sphericity of SUEP jet constituents in SUEP rest frame");
    suepTable->addColumnValue<float>("sphericityISR",     suep.getSphericityISR(),     "Sphericity of ISR jet constituents in ISR rest frame");
    suepTable->addColumnValue<int>  ("nConstituentsSUEP", suep.getNConstituentsSUEP(), "Track multiplicity of the SUEP AK15 jet");
    suepTable->addColumnValue<int>  ("nConstituentsISR",  suep.getNConstituentsISR(),  "Track multiplicity of the ISR AK15 jet");
    suepTable->addColumnValue<int>  ("nAK15Jets",         suep.getNAK15jets(),         "Number of AK15 jets in event");
    suepTable->addColumnValue<float>("SUEP_pt",           suep.ptSUEP(),               "pT of the SUEP AK15 jet (GeV)");
    suepTable->addColumnValue<float>("ISR_pt",            suep.ptISR(),                "pT of the ISR AK15 jet (GeV)");
    suepTable->addColumnValue<float>("Ht",                ht,                          "Scalar HT from AK4 jets with pT>30 GeV, |eta|<2.5");
    suepTable->addColumnValue<float>("maxElectronPt",     maxElePt,                    "Max scouting electron pT (GeV)");
    suepTable->addColumnValue<float>("maxMuonPt",         maxMuPt,                     "Max scouting muon pT (GeV)");
    iEvent.put(std::move(suepTable), "SuepEvent");

    // ── Track table (optional) ────────────────────────────────────────────────
    if (saveTracks_) {
        auto trkTable = std::make_unique<nanoaod::FlatTable>(trk_pt.size(), "Track", false);
        trkTable->addColumn<float>("pt",  trk_pt,  "PF candidate pT (GeV); pT>0.75, |eta|<2.4, PV0, charged hadron/e/mu");
        trkTable->addColumn<float>("eta", trk_eta, "PF candidate eta");
        iEvent.put(std::move(trkTable), "Track");
    }
}

void SUEPProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("ScoutingParticle", edm::InputTag("hltScoutingPFPacker"));
    desc.add<edm::InputTag>("ScoutingElectron", edm::InputTag("hltScoutingEgammaPacker"));
    desc.add<edm::InputTag>("ScoutingMuon",     edm::InputTag("hltScoutingMuonPacker"));
    desc.add<bool>("saveTracks", false);
    descriptions.addWithDefaultLabel(desc);
}

DEFINE_FWK_MODULE(SUEPProducer);
