#include "SUEPTron/HLTSCOUTNANOProducer/interface/SuepEvent.h"

#include <algorithm>
#include <vector>
#include "fastjet/ClusterSequence.hh"
#include "TMatrixDSym.h"
#include "TMatrixDSymEigen.h"
#include "Math/LorentzVector.h"
#include "Math/PxPyPzE4D.h"
#include "Math/Boost.h"
#include "Math/Vector3D.h"

SuepEvent::SuepEvent(const std::vector<fastjet::PseudoJet>& particles) {
    fastjet::JetDefinition jetdef(fastjet::antikt_algorithm, 1.5);
    fastjet::ClusterSequence cs(particles, jetdef);
    std::vector<fastjet::PseudoJet> jets = fastjet::sorted_by_pt(cs.inclusive_jets());
    NAK15jets = static_cast<int8_t>(jets.size());

    if (jets.size() > 1) {
        // assign SUEP = higher-multiplicity jet, ISR = the other
        size_t iSUEP = (jets[0].constituents().size() >= jets[1].constituents().size()) ? 0 : 1;
        size_t iISR  = 1 - iSUEP;

        SUEP = jets[iSUEP];
        ISR  = jets[iISR];
        NConstituentsSUEP = static_cast<int16_t>(SUEP.constituents().size());
        NConstituentsISR  = static_cast<int16_t>(ISR.constituents().size());

        ConstituentsSUEP.clear();
        for (auto const& pj : SUEP.constituents())
            ConstituentsSUEP.emplace_back(pj.px(), pj.py(), pj.pz(), pj.e());

        ConstituentsISR.clear();
        for (auto const& pj : ISR.constituents())
            ConstituentsISR.emplace_back(pj.px(), pj.py(), pj.pz(), pj.e());
    } else {
        SUEP = fastjet::PseudoJet();
        ISR  = fastjet::PseudoJet();
        NConstituentsSUEP = 0;
        NConstituentsISR  = 0;
    }
}

float SuepEvent::getSphericitySUEP() {
    return getSphericitySystem(ConstituentsSUEP, SUEP);
}

float SuepEvent::getSphericityISR() {
    return getSphericitySystem(ConstituentsISR, ISR);
}

float SuepEvent::ptSUEP() { return SUEP.pt(); }
float SuepEvent::ptISR()  { return ISR.pt();  }

int16_t SuepEvent::getNConstituentsSUEP() { return NConstituentsSUEP; }
int16_t SuepEvent::getNConstituentsISR()  { return NConstituentsISR;  }
int8_t  SuepEvent::getNAK15jets()         { return NAK15jets;         }

float SuepEvent::getSphericitySystem(std::vector<fastjet::PseudoJet> system,
                                     fastjet::PseudoJet systemJet) {
    if (system.empty()) return 0.f;

    float beta_x = systemJet.px() / systemJet.e();
    float beta_y = systemJet.py() / systemJet.e();
    float beta_z = systemJet.pz() / systemJet.e();
    ROOT::Math::Boost B(ROOT::Math::XYZVector(-beta_x, -beta_y, -beta_z));

    float s_xx = 0, s_yy = 0, s_zz = 0, s_xy = 0, s_xz = 0, s_yz = 0, p_mod = 0;
    for (auto& pj : system) {
        ROOT::Math::LorentzVector<ROOT::Math::PxPyPzE4D<double>> v(pj.px(), pj.py(), pj.pz(), pj.e());
        auto vb = B(v);
        s_xx += vb.px() * vb.px();
        s_yy += vb.py() * vb.py();
        s_zz += vb.pz() * vb.pz();
        s_xy += vb.px() * vb.py();
        s_xz += vb.px() * vb.pz();
        s_yz += vb.py() * vb.pz();
        p_mod += vb.px()*vb.px() + vb.py()*vb.py() + vb.pz()*vb.pz();
    }
    if (p_mod == 0) return 0.f;

    s_xx /= p_mod; s_yy /= p_mod; s_zz /= p_mod;
    s_xy /= p_mod; s_xz /= p_mod; s_yz /= p_mod;

    TMatrixDSym S(3);
    S(0,0)=s_xx; S(0,1)=s_xy; S(0,2)=s_xz;
    S(1,0)=s_xy; S(1,1)=s_yy; S(1,2)=s_yz;
    S(2,0)=s_xz; S(2,1)=s_yz; S(2,2)=s_zz;

    TVectorD vals = TMatrixDSymEigen(S).GetEigenValues();
    std::sort(vals.GetMatrixArray(), vals.GetMatrixArray() + 3);
    float sph = static_cast<float>(1.5 * (vals[0] + vals[1]));
    return (sph < 0) ? 0.f : sph;
}
