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
#include "DataFormats/Scouting/interface/Run3ScoutingEERecHit.h"
#include "DataFormats/Scouting/interface/Run3ScoutingEBRecHit.h"
#include "DataFormats/Scouting/interface/Run3ScoutingHBHERecHit.h"
#include "DataFormats/EcalDetId/interface/EEDetId.h"
#include "DataFormats/EcalDetId/interface/EBDetId.h"
#include "DataFormats/HcalDetId/interface/HcalDetId.h"

#include <memory>
#include <vector>

class RecHitProducer : public edm::global::EDProducer<> {
public:
    explicit RecHitProducer(const edm::ParameterSet&);
    ~RecHitProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions&);
    void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
    const edm::EDGetTokenT<std::vector<Run3ScoutingEERecHit>>   recHitEEToken_;
    const edm::EDGetTokenT<std::vector<Run3ScoutingEBRecHit>>   recHitEBToken_;
    const edm::EDGetTokenT<std::vector<Run3ScoutingHBHERecHit>> recHitHBHEToken_;
};

RecHitProducer::RecHitProducer(const edm::ParameterSet& iConfig)
    : recHitEEToken_  (consumes(iConfig.getParameter<edm::InputTag>("RecHitEE"))),
      recHitEBToken_  (consumes(iConfig.getParameter<edm::InputTag>("RecHitEB"))),
      recHitHBHEToken_(consumes(iConfig.getParameter<edm::InputTag>("RecHitHBHE")))
{
    produces<nanoaod::FlatTable>("RecHitEE");
    produces<nanoaod::FlatTable>("RecHitEB");
    produces<nanoaod::FlatTable>("RecHitHB");
    produces<nanoaod::FlatTable>("RecHitHE");
}

void RecHitProducer::produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup&) const {

    // ── ECAL Endcap ──────────────────────────────────────────────────────────
    edm::Handle<std::vector<Run3ScoutingEERecHit>> recHitsEE;
    iEvent.getByToken(recHitEEToken_, recHitsEE);

    std::vector<float> ee_energy, ee_time;
    std::vector<int>   ee_iX, ee_iY, ee_zside;
    for (auto const& rh : *recHitsEE) {
        EEDetId id(rh.detId());
        if (!EEDetId::validDetId(id.ix(), id.iy(), id.zside())) continue;
        ee_energy.push_back(rh.energy());
        ee_time.push_back(rh.time());
        ee_iX.push_back(id.ix());
        ee_iY.push_back(id.iy());
        ee_zside.push_back(id.zside());
    }
    auto eeTable = std::make_unique<nanoaod::FlatTable>(ee_energy.size(), "RecHitEE", false);
    eeTable->addColumn<float>("energy", ee_energy, "Crystal energy (GeV)");
    eeTable->addColumn<float>("time",   ee_time,   "Crystal time (ns)");
    eeTable->addColumn<int>  ("iX",     ee_iX,     "Crystal iX [1,100]");
    eeTable->addColumn<int>  ("iY",     ee_iY,     "Crystal iY [1,100]");
    eeTable->addColumn<int>  ("zside",  ee_zside,  "Endcap side (+1/-1)");
    iEvent.put(std::move(eeTable), "RecHitEE");

    // ── ECAL Barrel ───────────────────────────────────────────────────────────
    edm::Handle<std::vector<Run3ScoutingEBRecHit>> recHitsEB;
    iEvent.getByToken(recHitEBToken_, recHitsEB);

    std::vector<float> eb_energy, eb_time;
    std::vector<int>   eb_iPhi, eb_iEta, eb_zside;
    for (auto const& rh : *recHitsEB) {
        EBDetId id(rh.detId());
        if (!EBDetId::validDetId(id.ieta(), id.iphi())) continue;
        eb_energy.push_back(rh.energy());
        eb_time.push_back(rh.time());
        eb_iPhi.push_back(id.iphi());
        eb_iEta.push_back(id.ieta());
        eb_zside.push_back(id.zside());
    }
    auto ebTable = std::make_unique<nanoaod::FlatTable>(eb_energy.size(), "RecHitEB", false);
    ebTable->addColumn<float>("energy", eb_energy, "Crystal energy (GeV)");
    ebTable->addColumn<float>("time",   eb_time,   "Crystal time (ns)");
    ebTable->addColumn<int>  ("iPhi",   eb_iPhi,   "Crystal iPhi [1,360]");
    ebTable->addColumn<int>  ("iEta",   eb_iEta,   "Crystal iEta [-85,85]");
    ebTable->addColumn<int>  ("zside",  eb_zside,  "Barrel side (+1/-1)");
    iEvent.put(std::move(ebTable), "RecHitEB");

    // ── HCAL Barrel + Endcap ─────────────────────────────────────────────────
    edm::Handle<std::vector<Run3ScoutingHBHERecHit>> recHitsHBHE;
    iEvent.getByToken(recHitHBHEToken_, recHitsHBHE);

    std::vector<float> hb_energy, he_energy;
    std::vector<int>   hb_iPhi, hb_iEta, hb_zside;
    std::vector<int>   he_iPhi, he_iEta, he_zside;
    for (auto const& rh : *recHitsHBHE) {
        HcalDetId id(rh.detId());
        if (id.subdet() == 2) {
            he_energy.push_back(rh.energy());
            he_iPhi.push_back(id.iphi());
            he_iEta.push_back(id.ieta());
            he_zside.push_back(id.zside());
        } else {
            hb_energy.push_back(rh.energy());
            hb_iPhi.push_back(id.iphi());
            hb_iEta.push_back(id.ieta());
            hb_zside.push_back(id.zside());
        }
    }
    auto hbTable = std::make_unique<nanoaod::FlatTable>(hb_energy.size(), "RecHitHB", false);
    hbTable->addColumn<float>("energy", hb_energy, "Tower energy (GeV)");
    hbTable->addColumn<int>  ("iPhi",   hb_iPhi,   "Tower iPhi");
    hbTable->addColumn<int>  ("iEta",   hb_iEta,   "Tower iEta");
    hbTable->addColumn<int>  ("zside",  hb_zside,  "Barrel side (+1/-1)");
    iEvent.put(std::move(hbTable), "RecHitHB");

    auto heTable = std::make_unique<nanoaod::FlatTable>(he_energy.size(), "RecHitHE", false);
    heTable->addColumn<float>("energy", he_energy, "Tower energy (GeV)");
    heTable->addColumn<int>  ("iPhi",   he_iPhi,   "Tower iPhi");
    heTable->addColumn<int>  ("iEta",   he_iEta,   "Tower iEta");
    heTable->addColumn<int>  ("zside",  he_zside,  "Endcap side (+1/-1)");
    iEvent.put(std::move(heTable), "RecHitHE");
}

void RecHitProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("RecHitEE",   edm::InputTag("hltScoutingRecHitPacker", "EE",   "HLT"));
    desc.add<edm::InputTag>("RecHitEB",   edm::InputTag("hltScoutingRecHitPacker", "EB",   "HLT"));
    desc.add<edm::InputTag>("RecHitHBHE", edm::InputTag("hltScoutingRecHitPacker", "HBHE", "HLT"));
    descriptions.addWithDefaultLabel(desc);
}

DEFINE_FWK_MODULE(RecHitProducer);
