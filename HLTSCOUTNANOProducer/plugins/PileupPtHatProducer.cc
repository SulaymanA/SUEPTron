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
#include "SimDataFormats/PileupSummaryInfo/interface/PileupSummaryInfo.h"

#include <memory>
#include <vector>

// Stores the pt-hat of every in-time pileup interaction, one row per interaction.
// The standard NanoAOD puTable reduces this same vector to its maximum
// (Pileup_pthatmax); the QCD pileup-overlap weight needs the full list.
class PileupPtHatProducer : public edm::global::EDProducer<> {
public:
    explicit PileupPtHatProducer(const edm::ParameterSet&);
    ~PileupPtHatProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions&);
    void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
    const edm::EDGetTokenT<std::vector<PileupSummaryInfo>> puInfoToken_;
};

PileupPtHatProducer::PileupPtHatProducer(const edm::ParameterSet& iConfig)
    : puInfoToken_(consumes(iConfig.getParameter<edm::InputTag>("src")))
{
    produces<nanoaod::FlatTable>("PileupPtHat");
}

void PileupPtHatProducer::produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup&) const {

    edm::Handle<std::vector<PileupSummaryInfo>> puInfo;
    iEvent.getByToken(puInfoToken_, puInfo);

    std::vector<float> puPtHats;
    if (puInfo.isValid()) {
        // Only BX 0, matching the in-time pileup that puTable summarises
        for (auto const& pu : *puInfo) {
            if (pu.getBunchCrossing() != 0) continue;
            auto const& ptHats = pu.getPU_pT_hats();
            puPtHats.assign(ptHats.begin(), ptHats.end());
            break;
        }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(puPtHats.size(), "PileupPtHat", false);
    table->addColumn<float>("puPtHats", puPtHats, "pt-hat of each in-time pileup interaction", 10);
    iEvent.put(std::move(table), "PileupPtHat");
}

void PileupPtHatProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("slimmedAddPileupInfo"));
    descriptions.addWithDefaultLabel(desc);
}

DEFINE_FWK_MODULE(PileupPtHatProducer);
