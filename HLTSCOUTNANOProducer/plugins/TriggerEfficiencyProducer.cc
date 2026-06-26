#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Common/interface/TriggerNames.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "DataFormats/Scouting/interface/Run3ScoutingPFJet.h"
#include "DataFormats/Scouting/interface/Run3ScoutingMuon.h"
#include "DataFormats/Common/interface/TriggerResults.h"

#include "correction.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

// ── TriggerEfficiencyProducer ──────────────────────────────────────────────────
// Produces a singleton FlatTable ("TriggerTable") with two quantities needed for
// the DST_PFScouting_SingleMuon trigger efficiency measurement:
//
//   denominator  – reference trigger fired AND >=1 tight scouting muon
//   Ht_corrected – HT from JEC-corrected scouting jets (jetID + muon cleaning)
//
// JECs are applied via correctionlib (L1FastJet → L2Relative → L3Absolute).
// Set applyJEC=False to use raw pT (e.g. for quick cross-checks).

class TriggerEfficiencyProducer : public edm::global::EDProducer<> {
public:
    explicit TriggerEfficiencyProducer(const edm::ParameterSet&);
    ~TriggerEfficiencyProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions&);
    void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
    const edm::EDGetTokenT<std::vector<Run3ScoutingPFJet>>  jetToken_;
    const edm::EDGetTokenT<std::vector<Run3ScoutingMuon>>   muonToken_;
    const edm::EDGetTokenT<double>                           rhoToken_;
    const edm::EDGetTokenT<edm::TriggerResults>              trigToken_;
    const std::string singleMuonPath_;
    const bool applyJEC_;

    // correctionlib objects are thread-safe for evaluate() after construction
    std::unique_ptr<correction::CorrectionSet> jecSet_;
    const correction::Correction* corrL1FastJet_  {nullptr};
    const correction::Correction* corrL2Relative_ {nullptr};
    const correction::Correction* corrL3Absolute_ {nullptr};

    // Returns JEC-corrected pT by chaining L1 → L2 → L3
    float applyJECChain(float area, float eta, float phi, float rawPt, float rho) const;

    static bool passJetID(const Run3ScoutingPFJet& jet);
    static bool passMuonID(const Run3ScoutingMuon& mu);
};

TriggerEfficiencyProducer::TriggerEfficiencyProducer(const edm::ParameterSet& ps)
    : jetToken_        (consumes(ps.getParameter<edm::InputTag>("JetTag"))),
      muonToken_       (consumes(ps.getParameter<edm::InputTag>("MuonTag"))),
      rhoToken_        (consumes(ps.getParameter<edm::InputTag>("RhoTag"))),
      trigToken_       (consumes(ps.getParameter<edm::InputTag>("TriggerResults"))),
      singleMuonPath_  (ps.getParameter<std::string>("SingleMuonPath")),
      applyJEC_        (ps.getParameter<bool>("applyJEC"))
{
    produces<nanoaod::FlatTable>("TriggerTable");

    if (applyJEC_) {
        const std::string jecFile = ps.getParameter<std::string>("JECFile");
        const std::string jecTag  = ps.getParameter<std::string>("JECTag");
        jecSet_ = correction::CorrectionSet::from_file(jecFile);
        corrL1FastJet_  = jecSet_->at(jecTag + "_L1FastJet_AK4PFHLT").get();
        corrL2Relative_ = jecSet_->at(jecTag + "_L2Relative_AK4PFHLT").get();
        corrL3Absolute_ = jecSet_->at(jecTag + "_L3Absolute_AK4PFHLT").get();
    }
}

float TriggerEfficiencyProducer::applyJECChain(
        float area, float eta, float phi, float rawPt, float rho) const {
    // L1FastJet: (jetArea, eta, phi, rawPt, rho) → scale
    std::cout << "Initial Jet pT: " << rawPt << std::endl;
    float pt = rawPt * static_cast<float>(
        corrL1FastJet_->evaluate({(double)area, (double)eta, (double)phi, (double)rawPt, (double)rho}));
    // L2Relative: (eta, phi, l1Pt) → scale
    std::cout << "After L1 Jet pT: " << pt << std::endl;
    pt *= static_cast<float>(
        corrL2Relative_->evaluate({(double)eta, (double)phi, (double)pt}));
    // L3Absolute: (eta, l2Pt) → scale
    std::cout << "After L2 Jet pT: " << pt << std::endl;
    pt *= static_cast<float>(
        corrL3Absolute_->evaluate({(double)eta, (double)pt}));
    std::cout << "After L3 Jet pT: " << pt << std::endl;
    std::cout << "------------------------------" << std::endl;
    return pt;
}

bool TriggerEfficiencyProducer::passJetID(const Run3ScoutingPFJet& jet) {
    float aeta = std::abs(jet.eta());
    float totalE = jet.neutralHadronEnergy() + jet.HFHadronEnergy()
                 + jet.photonEnergy()        + jet.HFEMEnergy()
                 + jet.muonEnergy()          + jet.electronEnergy()
                 + jet.chargedHadronEnergy();
    if (totalE <= 0.f) return false;

    float NHF    = (jet.neutralHadronEnergy() + jet.HFHadronEnergy()) / totalE;
    float NEMF   = (jet.photonEnergy()        + jet.HFEMEnergy())     / totalE;
    float muFrac =  jet.muonEnergy()                                   / totalE;
    int chargedMult = jet.chargedHadronMultiplicity() + jet.HFHadronMultiplicity();
    int neutralMult = jet.neutralHadronMultiplicity() + jet.HFEMMultiplicity();
    int nconst = jet.chargedHadronMultiplicity() + jet.neutralHadronMultiplicity()
               + jet.muonMultiplicity()          + jet.electronMultiplicity()
               + jet.photonMultiplicity();

    if (aeta < 2.6f)  return NHF < 0.99f && NEMF < 0.90f && nconst > 1 && chargedMult > 0 && muFrac < 0.80f;
    if (aeta < 2.7f)  return NEMF < 0.99f && muFrac < 0.80f;
    if (aeta < 3.0f)  return NEMF < 0.99f && neutralMult > 1;
    if (aeta < 5.0f)  return NEMF < 0.10f;
    return false;
}

bool TriggerEfficiencyProducer::passMuonID(const Run3ScoutingMuon& mu) {
    return mu.pt()                            >  30.f  &&
           std::abs(mu.eta())                 <   0.8f &&
           std::abs(mu.trk_dxy())             <   0.2f &&
           std::abs(mu.trk_dz())              <   0.5f &&
           mu.normalizedChi2()                <   3.f  &&
           mu.nValidRecoMuonHits()            >   0    &&
           mu.nRecoMuonMatchedStations()      >   1    &&
           mu.nValidPixelHits()               >   0    &&
           mu.nTrackerLayersWithMeasurement() >   5;
}

void TriggerEfficiencyProducer::produce(
        edm::StreamID, edm::Event& iEvent, const edm::EventSetup&) const {

    // ── inputs ────────────────────────────────────────────────────────────────
    edm::Handle<std::vector<Run3ScoutingPFJet>> jets;
    edm::Handle<std::vector<Run3ScoutingMuon>>  muons;
    edm::Handle<double>                          rhoH;
    edm::Handle<edm::TriggerResults>             trigResults;

    iEvent.getByToken(jetToken_,  jets);
    iEvent.getByToken(muonToken_, muons);
    iEvent.getByToken(rhoToken_,  rhoH);
    iEvent.getByToken(trigToken_, trigResults);

    // ── denominator: SingleMuon DST fired + >=1 tight muon ───────────────────
    bool firedSingleMuon = false;
    if (trigResults.isValid()) {
        const edm::TriggerNames& names = iEvent.triggerNames(*trigResults);
        for (unsigned i = 0; i < trigResults->size(); ++i) {
            if (names.triggerName(i).find(singleMuonPath_) != std::string::npos &&
                trigResults->accept(i)) {
                firedSingleMuon = true;
                break;
            }
        }
    }

    bool denominator = false;
    if (firedSingleMuon && muons.isValid()) {
        for (auto const& mu : *muons) {
            if (passMuonID(mu)) { denominator = true; break; }
        }
    }

    // ── Ht_corrected: JEC-corrected jets, jetID + muon cleaning ──────────────
    float htCorrected = 0.f;
    float rho = (rhoH.isValid()) ? static_cast<float>(*rhoH) : 0.f;

    if (jets.isValid()) {
        for (auto const& jet : *jets) {
            if (!passJetID(jet)) continue;

            float corrPt = applyJEC_
                ? applyJECChain(jet.jetArea(), jet.eta(), jet.phi(), jet.pt(), rho)
                : jet.pt();

            // subtract any muon with dR < 0.4 from the jet 4-momentum
            float px = corrPt * std::cos(jet.phi());
            float py = corrPt * std::sin(jet.phi());
            float pz = corrPt * std::sinh(jet.eta());
            float e  = corrPt * std::cosh(jet.eta());

            if (muons.isValid()) {
                for (auto const& mu : *muons) {
                    float deta = jet.eta() - mu.eta();
                    float dphi = jet.phi() - mu.phi();
                    if (dphi >  M_PI) dphi -= 2.f * M_PI;
                    if (dphi < -M_PI) dphi += 2.f * M_PI;
                    if (std::sqrt(deta*deta + dphi*dphi) < 0.4f) {
                        float mupx = mu.pt() * std::cos(mu.phi());
                        float mupy = mu.pt() * std::sin(mu.phi());
                        float mupz = mu.pt() * std::sinh(mu.eta());
                        float mue  = std::sqrt(mupx*mupx + mupy*mupy + mupz*mupz
                                               + 0.10566f * 0.10566f);
                        px -= mupx; py -= mupy; pz -= mupz; e -= mue;
                    }
                }
            }

            float cleanedPt = std::sqrt(px*px + py*py);
            float denomPt   = std::sqrt(px*px + py*py + pz*pz);
            float cleanedEta = (denomPt > 0.f && cleanedPt > 0.f)
                               ? std::asinh(pz / cleanedPt) : 0.f;

            if (cleanedPt > 30.f && std::abs(cleanedEta) < 2.5f)
                htCorrected += cleanedPt;
        }
    }

    // ── output ────────────────────────────────────────────────────────────────
    auto table = std::make_unique<nanoaod::FlatTable>(1, "TriggerTable", true);
    table->addColumnValue<int>  ("denominator",  static_cast<int>(denominator),
        "Reference trigger (DST_PFScouting_SingleMuon) fired AND >=1 tight scouting muon");
    table->addColumnValue<float>("Ht_corrected", htCorrected,
        "JEC-corrected HT from scouting jets after jetID and muon cleaning (GeV)");
    iEvent.put(std::move(table), "TriggerTable");
}

void TriggerEfficiencyProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("JetTag",         edm::InputTag("hltScoutingPFPacker"));
    desc.add<edm::InputTag>("MuonTag",        edm::InputTag("hltScoutingMuonPackerVtx"));
    desc.add<edm::InputTag>("RhoTag",         edm::InputTag("hltFixedGridRhoFastjetAll"));
    desc.add<edm::InputTag>("TriggerResults", edm::InputTag("TriggerResults", "", "HLT"));
    desc.add<std::string>  ("SingleMuonPath", "DST_PFScouting_SingleMuon");
    desc.add<bool>         ("applyJEC",        false);
    desc.add<std::string>  ("JECFile",         "");
    desc.add<std::string>  ("JECTag",          "Winter24HLT_V1_MC");
    descriptions.addWithDefaultLabel(desc);
}

DEFINE_FWK_MODULE(TriggerEfficiencyProducer);
