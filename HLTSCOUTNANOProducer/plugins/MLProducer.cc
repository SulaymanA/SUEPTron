// MLProducer.cc
//
// CMSSW EDProducer that runs the ABCDDiScOTEC GNN via ONNX Runtime and writes
// the two discriminants (var1, var2) to a singleton FlatTable ("MLTable").
//
// Two ONNX model interfaces are supported, chosen from the model's input count:
//
//   (a) k-NN inside the model (export_onnx.py, SUEP/training) - 1 input:
//         Input  "pf_features"  float32  (N_particles, 16)
//         Output "scores"       float32  (2,)              [var1, var2]
//       The model builds the k-NN graph itself (eta-phi Euclidean distance, no phi
//       wrap, self excluded, TopK) exactly as in training, so no graph is built here.
//       Requires N_particles >= k + 1.
//
//   (b) legacy - 2 inputs, graph built in C++ (buildKNNGraph):
//         Input  "pf_features"  float32  (N_particles, 16)
//         Input  "edge_index"   int64    (2, N_particles * k)   [row0 = central, row1 = neighbour]
//         Output "scores"       float32  (2,)                   [var1, var2]
//
// The 16 PF features (must exactly match preprocess.py transform_chunk):
//   0  log(pt)
//   1  eta
//   2  phi
//   3  mass (PDG value from pdgId)
//   4  charge (from pdgId)
//   5  log(trk_pt)            neutral → log(0) = -inf
//   6  trk_eta  (absolute)    neutral → 0
//   7  trk_phi  (absolute)    neutral → 0
//   8  clamp(trk_dxy,-1,1)    neutral → 0
//   9  clamp(trk_dz,-1,1)     neutral → 0
//  10  quality (uint8 cast to float)
//  11  isPion        |pdgId|==211
//  12  isNeutralHad  |pdgId| ∈ {1,2,130}
//  13  isPhoton      |pdgId|==22
//  14  isMuon        |pdgId|==13
//  15  isElectron    |pdgId|==11

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "DataFormats/Scouting/interface/Run3ScoutingParticle.h"

#include "onnxruntime/onnxruntime_cxx_api.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <utility>

// ── helpers ───────────────────────────────────────────────────────────────────

static float pdgMass(int pdgId) {
    switch (std::abs(pdgId)) {
        case 211:  return 0.13957f;
        case 321:  return 0.49368f;
        case 2212: return 0.93827f;
        case 2112: return 0.93957f;
        case 130:  return 0.49761f;
        case 22:   return 0.f;
        case 11:   return 0.000511f;
        case 13:   return 0.10566f;
        case 111:  return 0.13498f;
        default:   return 0.f;
    }
}

static float pdgCharge(int pdgId) {
    int aId = std::abs(pdgId);
    // neutrals
    if (aId == 22 || aId == 130 || aId == 2112 || aId == 111 || aId == 310)
        return 0.f;
    // hadrons: charge follows sign of pdgId
    if (aId == 211 || aId == 321 || aId == 2212)
        return (pdgId > 0) ? 1.f : -1.f;
    // leptons: e-/μ- have POSITIVE pdgId but NEGATIVE charge
    if (aId == 11 || aId == 13)
        return (pdgId > 0) ? -1.f : 1.f;
    return 0.f;
}

static float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Replicate NanoAOD precision=N lossy compression (round to N mantissa bits).
// Used so trkPt/trkEta/trkPhi match the precision=6 values the model trained on.
static float nanoQuantize(float val, int precision) {
    if (val == 0.f) return 0.f;
    uint32_t ibits;
    std::memcpy(&ibits, &val, sizeof(ibits));
    int shift = 23 - precision;               // bits to discard from 23-bit mantissa
    uint32_t half = 1u << (shift - 1);        // 0.5 ULP for round-to-nearest
    uint32_t mask = ~((1u << shift) - 1u);    // zero the discarded bits
    ibits = (ibits + half) & mask;
    std::memcpy(&val, &ibits, sizeof(val));
    return val;
}

// ── MLProducer ────────────────────────────────────────────────────────────────

class MLProducer : public edm::global::EDProducer<> {
public:
    explicit MLProducer(const edm::ParameterSet&);
    ~MLProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions&);
    void produce(edm::StreamID, edm::Event&, const edm::EventSetup&) const override;

private:
    const edm::EDGetTokenT<Run3ScoutingParticleCollection> particleToken_;
    const edm::EDGetTokenT<float>                          htToken_;
    const int k_;
    const int nParticlesMax_;
    const float htMin_;

    // ONNX runtime objects — thread-safe for Run() after construction
    const Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memInfo_;
    bool knnInModel_ = false;   // true: 1-input model that builds its own k-NN graph

    // Feature extraction: returns flat (N, 16) float array
    std::vector<float> extractFeatures(
        const Run3ScoutingParticleCollection& particles, int nKeep) const;

    // KNN graph in η-φ: returns edge_index as flat (2, N*k) int64 array
    // edge_index[0..N*k-1]       = central-node indices (target in PyG convention)
    // edge_index[N*k..2*N*k-1]   = neighbour indices    (source in PyG convention)
    std::vector<int64_t> buildKNNGraph(
        const Run3ScoutingParticleCollection& particles, int nKeep, int k) const;
};

MLProducer::MLProducer(const edm::ParameterSet& ps)
    : particleToken_(consumes(ps.getParameter<edm::InputTag>("ParticleTag"))),
      htToken_     (mayConsume<float>(ps.getParameter<edm::InputTag>("HtTag"))),
      k_           (ps.getParameter<int>("k")),
      nParticlesMax_(ps.getParameter<int>("nParticlesMax")),  // must match training (1000)
      htMin_       (ps.getParameter<double>("htMin")),
      env_         (ORT_LOGGING_LEVEL_WARNING, "MLProducer"),
      memInfo_     (Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
    produces<nanoaod::FlatTable>("MLTable");

    const std::string modelPath = ps.getParameter<std::string>("ModelPath");
    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(1);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.EnableCpuMemArena();
    session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(), opts);

    const size_t nInputs = session_->GetInputCount();
    if (nInputs != 1 && nInputs != 2)
        throw cms::Exception("MLProducer") << "Unsupported ONNX model " << modelPath
                                           << ": expected 1 (pf_features) or 2 (pf_features, edge_index) inputs, got "
                                           << nInputs;
    knnInModel_ = (nInputs == 1);
    edm::LogInfo("MLProducer") << "Loaded " << modelPath << " - "
                               << (knnInModel_ ? "k-NN built inside the model (1 input)"
                                               : "legacy model, k-NN built in C++ (2 inputs)");
}

// ── feature extraction ────────────────────────────────────────────────────────

std::vector<float> MLProducer::extractFeatures(
        const Run3ScoutingParticleCollection& particles, int nKeep) const {

    std::vector<float> features(nKeep * 16, 0.f);

    for (int i = 0; i < nKeep; ++i) {
        const auto& p   = particles[i];
        int         aId = std::abs(p.pdgId());
        bool        neu = (pdgCharge(p.pdgId()) == 0.f);

        // absolute track kinematics — Run3ScoutingParticle may store relative values
        // (trk_pt_ = track_pt - particle_pt, trk_eta_ = track_eta - particle_eta, etc.)
        // The NanoAOD producer (Run3ScoutingParticleToRecoPFCandidateProducer) applies the
        // same correction; we must match it exactly so features agree with training.
        bool rel = p.relative_trk_vars();
        float trkPt  = nanoQuantize(rel ? p.trk_pt()  + p.pt()  : p.trk_pt(),  6);
        float trkEta = nanoQuantize(rel ? p.trk_eta() + p.eta() : p.trk_eta(), 6);
        float trkPhi = nanoQuantize(rel ? p.trk_phi() + p.phi() : p.trk_phi(), 6);

        float* row = features.data() + i * 16;
        row[ 0] = std::log(p.pt());
        row[ 1] = p.eta();
        row[ 2] = p.phi();
        row[ 3] = pdgMass(p.pdgId());
        row[ 4] = pdgCharge(p.pdgId());
        row[ 5] = std::log(trkPt > 0.f ? trkPt : p.pt());
        row[ 6] = trkEta;
        row[ 7] = trkPhi;
        row[ 8] = neu ? 0.f : clamp(p.dxy(), -1.f, 1.f);
        row[ 9] = neu ? 0.f : clamp(p.dz(),  -1.f, 1.f);
        row[10] = static_cast<float>(p.quality());
        row[11] = (aId == 211) ? 1.f : 0.f;
        row[12] = (aId == 1 || aId == 2 || aId == 130) ? 1.f : 0.f;
        row[13] = (aId == 22)  ? 1.f : 0.f;
        row[14] = (aId == 13)  ? 1.f : 0.f;
        row[15] = (aId == 11)  ? 1.f : 0.f;
    }
    return features;
}

// ── KNN graph in η-φ ─────────────────────────────────────────────────────────

std::vector<int64_t> MLProducer::buildKNNGraph(
        const Run3ScoutingParticleCollection& particles, int nKeep, int k) const {

    int nEdges = nKeep * k;
    // flat layout: [row0 (length nEdges), row1 (length nEdges)]
    std::vector<int64_t> edgeIndex(2 * nEdges, 0);
    int64_t* row0 = edgeIndex.data();            // central-node (target)
    int64_t* row1 = edgeIndex.data() + nEdges;   // neighbour   (source)

    int edgePos = 0;
    std::vector<std::pair<float,int>> dists;
    dists.reserve(nKeep - 1);
    for (int i = 0; i < nKeep; ++i) {
        float etaI = particles[i].eta();
        float phiI = particles[i].phi();

        // collect (ΔR², j) for all j≠i
        dists.clear();
        for (int j = 0; j < nKeep; ++j) {
            if (j == i) continue;
            float deta = etaI - particles[j].eta();
            float dphi = phiI - particles[j].phi();
            if (dphi >  static_cast<float>(M_PI)) dphi -= 2.f * static_cast<float>(M_PI);
            if (dphi < -static_cast<float>(M_PI)) dphi += 2.f * static_cast<float>(M_PI);
            dists.push_back({deta * deta + dphi * dphi, j});
        }

        int nNbr = std::min(k, static_cast<int>(dists.size()));
        std::nth_element(dists.begin(), dists.begin() + nNbr, dists.end());

        for (int n = 0; n < nNbr; ++n) {
            row0[edgePos] = static_cast<int64_t>(i);
            row1[edgePos] = static_cast<int64_t>(dists[n].second);
            ++edgePos;
        }
    }
    // trim to actual number of edges (can be < nEdges if nKeep < k+1)
    edgeIndex.resize(2 * edgePos);
    // fix: reset row1 pointer after resize (already ok since we built in-place)
    return edgeIndex;
}

// ── produce ───────────────────────────────────────────────────────────────────

void MLProducer::produce(
        edm::StreamID, edm::Event& iEvent, const edm::EventSetup&) const {

    edm::Handle<Run3ScoutingParticleCollection> particles;
    edm::Handle<float>                          htH;
    iEvent.getByToken(particleToken_, particles);
    iEvent.getByToken(htToken_,       htH);

    float var1 = -1.f, var2 = -1.f;
    const float htCorrected = (htH.isValid()) ? *htH : 0.f;
    const int nKeep = particles.isValid()
        ? std::min(static_cast<int>(particles->size()), nParticlesMax_) : 0;
    // The in-model k-NN (TopK over the other particles) needs at least k+1 particles
    const int nMin = knnInModel_ ? k_ + 1 : 1;
    if (nKeep >= nMin && htCorrected >= htMin_) {
        std::vector<float> features = extractFeatures(*particles, nKeep);
        std::array<int64_t, 2> featShape = {{static_cast<int64_t>(nKeep), 16}};

        std::vector<Ort::Value> inputTensors;
        inputTensors.push_back(Ort::Value::CreateTensor<float>(
            memInfo_, features.data(), features.size(),
            featShape.data(), featShape.size()));

        // legacy models only: graph built here (kept alive until Run returns)
        std::vector<int64_t>   edgeIndex;
        std::array<int64_t, 2> edgeShape = {{2, 0}};
        if (!knnInModel_) {
            edgeIndex = buildKNNGraph(*particles, nKeep, k_);
            edgeShape[1] = static_cast<int64_t>(edgeIndex.size()) / 2;
            inputTensors.push_back(Ort::Value::CreateTensor<int64_t>(
                memInfo_, edgeIndex.data(), edgeIndex.size(),
                edgeShape.data(), edgeShape.size()));
        }

        const char* inputNames[]  = {"pf_features", "edge_index"};
        const char* outputNames[] = {"scores"};
        auto outputTensors = session_->Run(
            Ort::RunOptions{nullptr},
            inputNames,  inputTensors.data(),  inputTensors.size(),
            outputNames, 1);

        const float* scores = outputTensors[0].GetTensorData<float>();
        var1 = scores[0];
        var2 = scores[1];
    }

    auto table = std::make_unique<nanoaod::FlatTable>(1, "MLTable", true);
    table->addColumnValue<float>("var1", var1,
        "ABCDDiScOTEC discriminant 1 (-1 if model not run)");
    table->addColumnValue<float>("var2", var2,
        "ABCDDiScOTEC discriminant 2 (-1 if model not run)");
    iEvent.put(std::move(table), "MLTable");
}

// ── descriptions ─────────────────────────────────────────────────────────────

void MLProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("ParticleTag", edm::InputTag("hltScoutingPFPacker"));
    desc.add<edm::InputTag>("HtTag",      edm::InputTag("TriggerTable", "htCorrected"));
    desc.add<std::string>("ModelPath",    "");
    desc.add<int>   ("k",             10);
    desc.add<int>   ("nParticlesMax", 1000);
    desc.add<double>("htMin",         0.0);
    descriptions.addWithDefaultLabel(desc);
}

DEFINE_FWK_MODULE(MLProducer);
